#include<stdio.h>
#include<string>
#include<io.h>
#include<vector>
#include<thread>
#define __STDC_CONSTANT_MACROS
using namespace std;
extern "C"
{
#include"libavformat\avformat.h"
#include"libavcodec\avcodec.h"
#include"libavutil\time.h"
#include"libavutil\log.h"
#include"SDL\SDL.h"
#include"libswscale\swscale.h"
#include"libswresample\swresample.h"
#include"libavutil\imgutils.h"
}
typedef struct STU_VIDEO_DATA
{
	int iW;
	int iH;

}STU_VIDEO_DATA;
//定义输出音频格式信息
typedef struct STU_FRAME_PARA
{
	int iChannels;
	int inb_Samples;
	int iFrameRate;
	int Format;
	int64_t i64channel_layout;

}STU_FRAME_PARA;

//定义音频真实数据存放结构体
typedef struct STU_DATA
{
	uint8_t *p;
	int iLen;
	double dpts;
	double duration;

	AVFrame pFrame;

}STU_DATA;

//定义数据列表
typedef struct AVFrameList
{
	STU_DATA p;
	AVFrameList *next;

}AVFrameList;

//
typedef struct STU_FRAME
{
	AVFrameList *pHead;
	AVFrameList *pEnd;
	int nb_Frame;
	SDL_mutex *sdl_mutex;
	SDL_cond *cond;

}STU_FRAME;
void queue_init(STU_FRAME *pStu)
{
	//memset(pStu,0,sizeof(pStu));
	pStu->pHead = nullptr;
	pStu->pEnd = nullptr;
	pStu->nb_Frame = 0;
	pStu->sdl_mutex = nullptr;
	pStu->cond = nullptr;

	pStu->sdl_mutex = SDL_CreateMutex();
	pStu->cond = SDL_CreateCond();
}

void queue_put(STU_FRAME *pStu, STU_DATA *pFrame)
{
	AVFrameList *pFramList = nullptr;
	
	pFramList = (AVFrameList *)av_malloc(sizeof(AVFrameList));

	pFramList->p = *pFrame;
	pFramList->next = nullptr;

	SDL_LockMutex(pStu->sdl_mutex);
	if (pStu->pEnd == nullptr || pStu->pHead == nullptr)
	{
		pStu->pHead = pFramList;
	}
	else
	{
		pStu->pEnd->next = pFramList;
	}
	
	pStu->pEnd = pFramList;
	pStu->pEnd->next = nullptr;

	pStu->nb_Frame++;

	SDL_CondSignal(pStu->cond);

	SDL_UnlockMutex(pStu->sdl_mutex);

}
bool queue_get(STU_FRAME *pStu, STU_DATA *pFrame)
{
	AVFrameList *pList = nullptr;
	bool breturn = false;

	SDL_LockMutex(pStu->sdl_mutex);

	for (;;)
	{
		if (pStu->nb_Frame == 0)
		{
			SDL_CondWait(pStu->cond, pStu->sdl_mutex);
		}

		pList = pStu->pHead;
		if (pList == nullptr)
		{
			breturn = false;
			pStu->pEnd = nullptr;

			break;
		}
		
		pStu->pHead = pList->next;
		pStu->nb_Frame--;
		
		*pFrame = pList->p;
		
		av_free(pList);

		breturn = true;
		break;
	}

	SDL_UnlockMutex(pStu->sdl_mutex);
	return breturn;
}
int callback_cb(void *p)
{
	return 1;
}

double g_pts = 0;
void g_initPts()
{
}
void g_unintPts()
{
}
void g_setPtsTime(double pts)
{
	g_pts = pts;
}

void g_getPtsTime(double *pts)
{
	*pts = g_pts;
}
void __cdecl AudioCallback(void *userdata, Uint8 * stream,
	int len)
{
	STU_FRAME *pList = (STU_FRAME *)userdata;

	static uint8_t audio_buf[8192 * 2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;

	int audio_size = 0;
	STU_DATA pFrame{nullptr,0};
	
	double pts = 0;
	double duration = 0;

	while (len > 0)
	{
		if (audio_buf_index >= audio_buf_size)//数组耗尽
		{
			if (queue_get(pList, &pFrame))//获取数据
			{
				audio_buf_size = pFrame.iLen;
				memcpy(audio_buf, pFrame.p, audio_buf_size);
				pts = pFrame.dpts;
			}
			else//获取数据失败,填充静音
			{
				audio_buf_size = 1024;
				memset(audio_buf, 0, audio_buf_size);
			}
			audio_buf_index = 0;
		}
		//
		audio_size = audio_buf_size - audio_buf_index;
		if (audio_size > len)
		{
			audio_size = len;
		}
		
		memcpy(stream, (uint8_t*)(audio_buf + audio_buf_index), audio_size);
		
		len -= audio_size;//剩余有效空间
		stream += audio_size;//
		audio_buf_index += audio_size;//数据有效位置偏移
	}
	//更新音频播出时间
	double t = (double)(2*8192 + audio_buf_size - audio_buf_index) / double(2 * 44100 * 2);
	pts -= t;

	g_setPtsTime(pts);
}
double compute_target_delay(double duration,double pts)
{
	//获取音频当前播放的pts
	double pts_a = 0;
	double target_delay = duration;//需要的延迟时间

	g_getPtsTime(&pts_a);

	//获取当前播放的视频时间点与音频时间点差值
	double diff = pts - pts_a;

	//获取阀值
	double sync_threshold = FFMAX(0.04, FFMIN(0.1, duration));

	if (diff < -sync_threshold)//视频比音频慢.
	{
		target_delay = FFMAX(0,target_delay + diff);//减少延迟时间
	}
	else if (diff >= sync_threshold && duration > 0.1)
	{
		target_delay = target_delay + diff;
	}
	else if (diff >= sync_threshold)//视频比音频快
	{
		target_delay = 2 * duration;//增大延迟时间.
	}

	return target_delay;
}

void thread_video_display(AVCodecContext *pCodecCtx,SDL_Renderer *pRender,SDL_Texture *pTexture,STU_FRAME *pQueue_video,STU_VIDEO_DATA *pRaram)
{
	SDL_Renderer *pRend = pRender;
	SDL_Texture *pTex = pTexture;
	
	STU_DATA stu;

	double frame_time_start = 0;//第一帧起始时间点微妙
	double frame_last_play = 0;//上一帧播放的时间点
	double frame_last_duration = 0;//上一帧播放的持续时间
	double frame_pts = 0;//当前帧的pts
	AVRational tb{1,90000};	//基础时间戳

	while (true)
	{
		if (queue_get(pQueue_video, &stu))
		{
			if (pTex == nullptr)
			{
				//该fromat类型需要和转换后的类型相同，否则绘制出错.
				pTex = SDL_CreateTexture(pRend, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
				if (pTex == nullptr)
				{
					printf("");
				}
			}
			//同步
			if (frame_time_start == 0)
			{
				frame_time_start = av_gettime_relative();//获取指定时间毫秒.
			}
			//获取当前帧的pts.单位时间为秒，一般为1/帧率
			if (stu.pFrame.best_effort_timestamp == AV_NOPTS_VALUE)
			{
				frame_pts = 0;
			}
			else
			{
				frame_pts = stu.pFrame.best_effort_timestamp * av_q2d(tb);
			}
			
			printf("video pts= %f,pts=%lld\n", frame_pts,stu.pFrame.pts);
			//获取上一帧的持续时间单位秒
			if (frame_last_play == 0)
			{
				frame_last_duration = (pCodecCtx->framerate.num && pCodecCtx->framerate.den ? av_q2d({ pCodecCtx->framerate.den,pCodecCtx->framerate.num }) : 0);
			}
			else
			{
				frame_last_duration = frame_pts - frame_last_play;
			}
			if (frame_last_duration <= 0 || frame_last_duration > 1.0)
			{
				frame_last_duration = (pCodecCtx->framerate.num && pCodecCtx->framerate.den ? av_q2d({ pCodecCtx->framerate.den,pCodecCtx->framerate.num }) : 0);
			}
			frame_last_play = frame_pts;//用当前播放时间点更新该变量

			//获取需要的延迟时间单位秒
			double delay = compute_target_delay(frame_last_duration, frame_pts);

			double time = av_gettime_relative();//获取当前时间

			if (time < frame_time_start + delay)//判断当前时间是否小于上次播放时间加上
			{

			}
			printf("delay=%f\n",delay);

			//
			SDL_UpdateTexture(pTex, NULL, stu.pFrame.data[0], stu.pFrame.linesize[0]);
			SDL_RenderClear(pRend);
			SDL_RenderCopy(pRend, pTex, NULL, NULL);
			SDL_RenderPresent(pRend);

			//延迟时间微妙
			av_usleep(delay * 1000000);
		}
	}
}
void thread_video(AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx,SDL_Renderer *pRender,SDL_Texture *pTexture,STU_VIDEO_DATA *pParam, STU_FRAME *pQueue_video)
{
	AVPacket pkt;
	AVFrame *pFrame = nullptr;
	AVFrame *pFDest = nullptr;
	uint8_t *pBuffer = nullptr;

	SwsContext *pswsContex = nullptr;

	FILE *pFile = nullptr;

	fopen_s(&pFile, "video.yuv", "wb+");

	pFrame = av_frame_alloc();

	while (av_read_frame(pFormatCtx, &pkt) == 0)
	{
		int ir = avcodec_send_packet(pCodecCtx, &pkt);
		if (ir == 0 || ir == AVERROR(EAGAIN))
		{
			if (pswsContex == nullptr)
			{
				pswsContex = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
					pParam->iW, pParam->iH, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, 0);
				if (pswsContex == nullptr)
				{
					printf("");
				}
			}
			if (pFDest == nullptr)
			{
				pFDest = av_frame_alloc();

				pBuffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pParam->iW, pParam->iH, 1));

				av_image_fill_arrays(pFDest->data, pFDest->linesize, pBuffer, AV_PIX_FMT_YUV420P, pParam->iW, pParam->iH, 1);
			}
			while (avcodec_receive_frame(pCodecCtx, pFrame) == 0)
			{
				//printf("Video pts=%d.\n",pFrame->pts);

				int ih = sws_scale(pswsContex, 
					(const uint8_t *const *)pFrame->data, 
					pFrame->linesize,
					0, 
					pCodecCtx->height, 
					(uint8_t *const*)pFDest->data, 
					pFDest->linesize);
#if 0				
				fwrite(pFDest->data[0], 1, pCodecCtx->width * pCodecCtx->height, pFile);//y
				fwrite(pFDest->data[1], 1, pCodecCtx->width * pCodecCtx->height / 4, pFile);//u
				fwrite(pFDest->data[2], 1, pCodecCtx->width * pCodecCtx->height / 4, pFile);//v
#endif

				STU_DATA stu;
				pFDest->best_effort_timestamp = pFrame->best_effort_timestamp;
				pFDest->pts = pFrame->pts;

				stu.pFrame = *pFDest;

				queue_put(pQueue_video, &stu);
			}
			av_frame_unref(pFrame);
		}
		av_packet_unref(&pkt);
	}
	av_frame_free(&pFrame);

	sws_freeContext(pswsContex);

	if (pBuffer != nullptr)
	{
		av_free(pBuffer);
	}
	fclose(pFile);

	printf("thread_video end.\n");
}
void __cdecl thread_audio(AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, STU_FRAME *pQueue, STU_FRAME_PARA *pPara,STU_FRAME_PARA *pTarget)
{
	AVPacket pkt;
	AVFrame *pFrame = nullptr;
	FILE *pA = nullptr;

	fopen_s(&pA, "audio.pcm", "wb+");
	
	SwrContext *pswrCtx = nullptr;

	pFrame = av_frame_alloc();

	STU_DATA stu_data{nullptr,0};

	int64_t i64NextTime = 0;//用于记录下一帧音频时间戳
	AVRational next_time_base;

	while (av_read_frame(pFormatCtx, &pkt) == 0)
	{
		int ir = avcodec_send_packet(pCodecCtx, &pkt);
		if (ir == 0 || ir == AVERROR(EAGAIN))
		{
			while (avcodec_receive_frame(pCodecCtx, pFrame) == 0)
			{
				if (pFrame->nb_samples != pPara->inb_Samples || pFrame->channel_layout != pPara->i64channel_layout
					|| pCodecCtx->sample_fmt != pPara->Format || pFrame->sample_rate != pPara->iFrameRate)
				{
					swr_free(&pswrCtx);

					pswrCtx = swr_alloc_set_opts(NULL, 
						pPara->i64channel_layout,
						(AVSampleFormat)pPara->Format,
						pPara->iFrameRate,
						pFrame->channel_layout,
						pCodecCtx->sample_fmt,
						pFrame->sample_rate, 
						0, NULL);
					if (!pswrCtx || swr_init(pswrCtx) < 0)
					{
						printf("swr_init failed\n");
						return;
					}

					pPara->Format = pCodecCtx->sample_fmt;
					pPara->i64channel_layout = pFrame->channel_layout;
					pPara->iChannels = pFrame->channels;
					pPara->iFrameRate = pFrame->sample_rate;

				}

				if (pswrCtx != nullptr)//进行重采样
				{
					const uint8_t **in = (const uint8_t **)pFrame->extended_data;
					uint8_t *audio_buf1 = nullptr;
					unsigned int buf_size = 0;

					uint8_t **out = &audio_buf1;
					int out_size = av_samples_get_buffer_size(NULL, pTarget->iChannels, pTarget->inb_Samples, (AVSampleFormat)pTarget->Format, 0);

					av_fast_malloc(&audio_buf1, &buf_size, out_size);

					int len = swr_convert(pswrCtx, out, pTarget->inb_Samples, in, pFrame->nb_samples);

					int resampled_data_size = len * pTarget->iChannels * av_get_bytes_per_sample((AVSampleFormat)pTarget->Format);

					stu_data.iLen = resampled_data_size;
					stu_data.p = audio_buf1;
				}

				//
				AVRational tb{1,90000};
				AVRational tR{ 1,pFrame->sample_rate };
				AVRational t{1,AV_TIME_BASE};

				//将pts转换成以采样率为时间戳的数值
				if (pFrame->pts != AV_NOPTS_VALUE)
				{
					pFrame->pts = av_rescale_q(pFrame->pts, tb, tR);
				}
				else if (i64NextTime != AV_NOPTS_VALUE)
				{
					pFrame->pts = av_rescale_q(i64NextTime, next_time_base, tR);
				}
				if (pFrame->pts != AV_NOPTS_VALUE)
				{
					i64NextTime = pFrame->pts + pFrame->nb_samples;
					next_time_base = tR;
				}

				//根据转换后的数值将数值转换成当前对应的时间.
				stu_data.dpts = pFrame->pts * av_q2d(tR) + double(stu_data.iLen) /double(pFrame->sample_rate * pFrame->channels *av_get_bytes_per_sample((AVSampleFormat)pTarget->Format));
				stu_data.duration = av_q2d({ pFrame->nb_samples,pFrame->sample_rate });
				
				queue_put(pQueue, &stu_data);
#if 0				
				int data_size = av_get_bytes_per_sample(pCodecCtx->sample_fmt);//4
				for (int i = 0; i < pFrame->nb_samples; i++)	//1024
				{
					for (int j = 0; j < pFrame->channels; j++)//2
					{
						fwrite(pFrame->data[j] + data_size * i, 1, data_size, pA);
					}
				}
#endif				

			}
			av_frame_unref(pFrame);
		}
		av_packet_unref(&pkt);
	}
	
	av_frame_free(&pFrame);

	swr_free(&pswrCtx);

	fclose(pA);

	printf("thread_audio end.\n");
}
#undef main
int main(int argc, char *argv[])
{
	av_register_all();
	
	avformat_network_init();

	FILE *pFile = nullptr;
	FILE *pFile_a = nullptr;

	char *path = "video.264";
	char *path_a = "audio.aac";


	char *url = "rtp://127.0.0.1:1234";
	char *url_a = "rtp://127.0.0.1:1236";

	errno_t er =fopen_s(&pFile, path, "wb+");
	if (er != 0)
	{
		printf("");
	}
	er = fopen_s(&pFile_a, path_a, "wb+");
	if (er != 0)
	{
		printf("");
	}
	
	AVFormatContext *pFormatCtx = nullptr;
	AVFormatContext *pFormatCtx_a = nullptr;

	int ir = avformat_open_input(&pFormatCtx, url, nullptr, nullptr);
	if (ir < 0)
	{
		printf("");
	}

	ir = avformat_open_input(&pFormatCtx_a, url_a, nullptr, nullptr);
	if (ir < 0)
	{
		printf("");
	}

	ir = avformat_find_stream_info(pFormatCtx, nullptr);
	if (ir < 0)
	{
		printf("");
	}

	ir = avformat_find_stream_info(pFormatCtx_a, nullptr);
	if (ir < 0)
	{
		printf("");
	}

	int iIndex_v = -1;
	int iIndex_a = -1;

	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			iIndex_v = i;
			break;
		}
	}

	for (int i = 0; i < pFormatCtx_a->nb_streams; i++)
	{
		if (pFormatCtx_a->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			iIndex_a = i;
			break;
		}
	}

	AVCodecContext *pCodecCtx = nullptr;
	AVCodecContext *pCodecCtx_a = nullptr;

	AVCodec *pCodec = nullptr;
	AVCodec *pCodec_a = nullptr;

	pCodec = avcodec_find_decoder(pFormatCtx->streams[iIndex_v]->codecpar->codec_id);
	if (pCodec == nullptr)
	{
		printf("");
	}

	pCodecCtx = avcodec_alloc_context3(nullptr);
	if (pCodecCtx == nullptr)
	{
		printf("");
	}

	ir = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[iIndex_v]->codecpar);
	if (ir < 0)
	{
		printf("");
	}

	ir = avcodec_open2(pCodecCtx, pCodec,nullptr);
	if (ir != 0)
	{
		printf("");
	}

	pCodec_a = avcodec_find_decoder(pFormatCtx_a->streams[iIndex_a]->codecpar->codec_id);
	if (pCodec_a == nullptr)
	{
		printf("");
	}

	pCodecCtx_a = avcodec_alloc_context3(nullptr);
	if (pCodecCtx_a == nullptr)
	{
		printf("");
	}

	ir = avcodec_parameters_to_context(pCodecCtx_a, pFormatCtx_a->streams[iIndex_a]->codecpar);
	if (ir < 0)
	{
		printf("");
	}

	ir = avcodec_open2(pCodecCtx_a, pCodec_a, nullptr);
	if (ir != 0)
	{
		printf("");
	}
	
	printf("*********************\n");
	av_dump_format(pFormatCtx, 0, url, 0);
	av_dump_format(pFormatCtx_a, 0, url_a, 0);
	printf("*********************\n");
	
	STU_FRAME pQueue_audio,pQueue_video;

	queue_init(&pQueue_audio);
	queue_init(&pQueue_video);

	int iSDL = SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER);
	if (iSDL)
	{
		printf("");
	}

	SDL_AudioSpec wanted_spec, spec;
	memset(&wanted_spec,0,sizeof(SDL_AudioSpec));
	memset(&spec,0,sizeof(SDL_AudioSpec));

	wanted_spec.callback = AudioCallback;//回调函数
	wanted_spec.channels = 2;//声音通道数
	wanted_spec.format = AUDIO_S16SYS;//音频数据格式
	wanted_spec.freq = 44100;//采样率
	wanted_spec.samples = 1024;//sample占用字节
	wanted_spec.silence = 0;//静音值
	wanted_spec.userdata = &pQueue_audio;//SDL回调参数

	iSDL = SDL_OpenAudio(&wanted_spec, &spec);
	if (iSDL == 0)
	{
		printf("channe=%d,size=%d\n",spec.channels,spec.size);
	}
	
	STU_FRAME_PARA sdl_frame_para,sdl_frame_target;

	sdl_frame_para.Format = AV_SAMPLE_FMT_S16;
	sdl_frame_para.iChannels = spec.channels;
	sdl_frame_para.iFrameRate = spec.freq;
	sdl_frame_para.inb_Samples = spec.samples;
	sdl_frame_para.i64channel_layout = av_get_default_channel_layout(spec.channels);
	
	memcpy(&sdl_frame_target, &sdl_frame_para, sizeof(STU_FRAME_PARA));

	SDL_PauseAudio(0);

	STU_VIDEO_DATA stu_video_data{0,0};

	stu_video_data.iH = 240;
	stu_video_data.iW = 320;

	SDL_Window *sdl_window = SDL_CreateWindow("Video", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, stu_video_data.iW, stu_video_data.iH, SDL_WINDOW_OPENGL);
	if (sdl_window == nullptr)
	{
		printf("");
	}

	
	SDL_Renderer *sdl_render = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_SOFTWARE);
	if (sdl_render == nullptr)
	{
		printf("");
	}

	SDL_Texture *sdl_texture = nullptr;
	//SDL_CreateTexture()
	
	g_initPts();

	thread thread_v(thread_video, pFormatCtx, pCodecCtx,sdl_render,sdl_texture,&stu_video_data,&pQueue_video);
	
	thread thread_a(thread_audio, pFormatCtx_a, pCodecCtx_a, &pQueue_audio,&sdl_frame_para,&sdl_frame_target);

	thread thread_v_display(thread_video_display, pCodecCtx,sdl_render,sdl_texture,&pQueue_video,&stu_video_data);

	thread_v.detach();
	thread_a.detach();
	thread_v_display.detach();

	SDL_Event envent;

	bool br = true;
	while (br)
	{
		int ie = SDL_WaitEvent(&envent);
		switch (envent.type)
		{
			case SDL_QUIT:
			{
				br = false;
			}
			default:
			{

			}
			break;
		}
	}
#if 0
	AVPacket pkt;
	AVFrame *pfa;

	pfa = av_frame_alloc();

	while (av_read_frame(pFormatCtx, &pkt) == 0)
	{
		if (pkt.stream_index == iIndex_v)
		{
			int ire = fwrite(pkt.data, 1, pkt.size, pFile);
			printf("reserv Len=%d\n", ire);
			
			ire = avcodec_send_packet(pCodecCtx, &pkt);
			if (ire == 0)
			{
				while (avcodec_receive_frame(pCodecCtx, pfa) == 0)
				{
					printf("AVFrame H=%d,W=%d\n",pfa->height,pfa->width);
					
					av_frame_unref(pfa);
				}

			}
			else if (ire == AVERROR(EAGAIN))
			{
				printf("123");
			}
			else if (ire == AVERROR_EOF)
			{
				printf("11");
			}
			else if (ire == AVERROR(ENOMEM))
			{
				printf("222");
			}
			else {
				printf("00");
			}

		}
		av_packet_unref(&pkt);
	}
#endif

	avcodec_free_context(&pCodecCtx);
	avcodec_free_context(&pCodecCtx_a);
	avformat_close_input(&pFormatCtx);
	avformat_close_input(&pFormatCtx_a);

	fclose(pFile);
	fclose(pFile_a);

	return 0;

}