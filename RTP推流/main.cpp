#include<stdio.h>
#include<string>
#include<io.h>
#include<vector>
#define __STDC_CONSTANT_MACROS
using namespace std;
extern "C"
{
#include"libavformat\avformat.h"
#include"libavcodec\avcodec.h"
#include"libavutil\time.h"
#include"libavutil\log.h"
}
int callback_cb(void *p)
{
	AVFormatContext *pFormatCtx = (AVFormatContext *)p;

	return 1;
}
int main(int argc, char *argv[])
{
	av_register_all();

	avformat_network_init();

	char *path_v = "q.264";
	char *path_a = "q.aac";

	char *url_v = "rtp://127.0.0.1:1234";
	char *url_a = "rtp://127.0.0.1:1236";

	AVFormatContext *pFormatCtxV = nullptr;
	AVFormatContext *pFormatCtxA = nullptr;
	AVFormatContext *pFormatCtxA_O = nullptr;
	AVFormatContext *pFormatCtxV_O = nullptr;

	int64_t cur_pts_v = 0, cur_pts_a = 0;

	int re = avformat_open_input(&pFormatCtxV, path_v, nullptr, nullptr);
	if (re != 0)
	{
		printf("");
	}
	//pFormatCtxV->interrupt_callback.callback = callback_cb;
	//pFormatCtxV->interrupt_callback.opaque = pFormatCtxV;

	re = avformat_open_input(&pFormatCtxA, path_a, nullptr, nullptr);
	if (re != 0)
	{
		printf("");
	}

	re = avformat_find_stream_info(pFormatCtxV, nullptr);
	if (re < 0)
	{
		printf("");
	}

	re = avformat_find_stream_info(pFormatCtxA, nullptr);
	if (re < 0)
	{
		printf("");
	}

	//
	printf("******************\n");
	av_dump_format(pFormatCtxV, 0, path_v, 0);
	av_dump_format(pFormatCtxA, 0, path_a, 0);
	printf("******************\n");
	//

	int iIndex_v = -1, iIndex_a = -1;

	for (int i = 0; i < pFormatCtxV->nb_streams; i++)
	{
		if (pFormatCtxV->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			iIndex_v = i;
			break;
		}
	}
	for (int i = 0; i < pFormatCtxA->nb_streams; i++)
	{
		if (pFormatCtxA->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			iIndex_a = i;
			break;
		}
	}

	//
	re = avformat_alloc_output_context2(&pFormatCtxA_O, nullptr, "rtp_mpegts", nullptr);
	if (re < 0)
	{
		printf("");
	}

	re = avformat_alloc_output_context2(&pFormatCtxV_O, nullptr, "rtp_mpegts", nullptr);
	if (re < 0)
	{
		printf("");
	}

	AVCodecContext *pCodecCtxV = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(pCodecCtxV, pFormatCtxV->streams[iIndex_v]->codecpar);

	//AVFormatContext内部申请AVStream结构体.
	AVStream *pV = avformat_new_stream(pFormatCtxV_O, pCodecCtxV->codec);
	
	//AVstream内部对应的codecpar赋值.
	re = avcodec_parameters_copy(pV->codecpar, pFormatCtxV->streams[iIndex_v]->codecpar);
	if (re < 0)
	{
		printf("");
	}

	AVCodecContext *pCodecCtxA = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(pCodecCtxA, pFormatCtxA->streams[iIndex_a]->codecpar);

	AVStream *pA = avformat_new_stream(pFormatCtxA_O, pCodecCtxA->codec);
	re = avcodec_parameters_copy(pA->codecpar, pFormatCtxA->streams[iIndex_a]->codecpar);
	if (re < 0)
	{
		printf("");
	}

	//
	printf("****************\n");
	av_dump_format(pFormatCtxA_O, 0, url_a, 1);
	av_dump_format(pFormatCtxV_O, 0, url_v, 1);
	printf("****************\n");

	re = avio_open(&pFormatCtxA_O->pb, url_a, AVIO_FLAG_WRITE);
	if (re < 0)
	{
		printf("");
	}
	
	re = avio_open(&pFormatCtxV_O->pb, url_v, AVIO_FLAG_WRITE);
	if (re < 0)
	{
		printf("");
	}

	//
	re = avformat_write_header(pFormatCtxV_O, nullptr);

	re = avformat_write_header(pFormatCtxA_O, nullptr);

	AVPacket pkt;
	int iFrame_index = 0;
	int iAFrame_index = 0;
	int64_t i64Start = av_gettime();

	while (true)
	{
		if (av_compare_ts(cur_pts_v, pFormatCtxV->streams[iIndex_v]->time_base, cur_pts_a, pFormatCtxA->streams[iIndex_a]->time_base) < 0)
		{
			re = av_read_frame(pFormatCtxV, &pkt);
			if (re < 0)
			{
				printf("");
				break;
			}
			if (pkt.stream_index == iIndex_v)
			{
				AVRational time_base = pFormatCtxV->streams[iIndex_v]->time_base;
				AVRational r_famerate = pFormatCtxV->streams[iIndex_v]->r_frame_rate;
				AVRational time_base_q = {1,AV_TIME_BASE};

				int64_t calc_duation = double(AV_TIME_BASE) / av_q2d(r_famerate);

				pkt.pts = av_rescale_q(iFrame_index *calc_duation, time_base_q, time_base);
				pkt.dts = pkt.pts;
				pkt.duration = av_rescale_q(calc_duation, time_base_q, time_base);
				
				cur_pts_v = pkt.pts;

				iFrame_index++;
				
				{//发送延时

					AVRational time_base = pFormatCtxV->streams[iIndex_v]->time_base;
					AVRational time_base_q = {1,AV_TIME_BASE};
					int64_t pts_time = av_rescale_q(pkt.pts, time_base, time_base_q);
					int64_t now_time = av_gettime() - i64Start;
					if (pts_time > now_time)
					{
						av_usleep(pts_time - now_time);
					}
				}
				pkt.pts = av_rescale_q_rnd(pkt.pts, time_base, pFormatCtxV_O->streams[0]->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
				pkt.dts = av_rescale_q_rnd(pkt.dts, time_base, pFormatCtxV_O->streams[0]->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
				pkt.duration = av_rescale_q(pkt.duration,time_base, pFormatCtxV_O->streams[0]->time_base);

				av_interleaved_write_frame(pFormatCtxV_O, &pkt);
			}
			av_packet_unref(&pkt);

		}
		else
		{
			re = av_read_frame(pFormatCtxA, &pkt);
			if (re < 0)
			{
				printf("");
				break;
			}
			if (pkt.stream_index == iIndex_a)
			{
				AVRational time_base = pFormatCtxA->streams[iIndex_a]->time_base;
				AVRational time_base_q = {1,AV_TIME_BASE};
				
				double frame_size = (double)pFormatCtxA->streams[iIndex_a]->codecpar->frame_size;
				double frame_rate = (double)pFormatCtxA->streams[iIndex_a]->codecpar->sample_rate;
				
				int64_t calc_duration = (double)(AV_TIME_BASE) *(frame_size / frame_rate);
				pkt.pts = av_rescale_q(iAFrame_index *calc_duration, time_base_q,time_base);
				pkt.dts = pkt.pts;
				pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);

				iAFrame_index++;
				cur_pts_a = pkt.pts;
			
				pkt.pts = av_rescale_q_rnd(pkt.pts, pFormatCtxA->streams[iIndex_a]->time_base, pFormatCtxA_O->streams[0]->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
				pkt.dts = av_rescale_q_rnd(pkt.dts, pFormatCtxA->streams[iIndex_a]->time_base, pFormatCtxA_O->streams[0]->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
				pkt.duration = av_rescale_q(pkt.duration, pFormatCtxA->streams[iIndex_a]->time_base, pFormatCtxA_O->streams[0]->time_base);
				av_interleaved_write_frame(pFormatCtxA_O, &pkt);
			}
			av_packet_unref(&pkt);
		}
		
	}
	
	av_write_trailer(pFormatCtxA_O);
	av_write_trailer(pFormatCtxV_O);


	avformat_close_input(&pFormatCtxA);
	avformat_close_input(&pFormatCtxV);

	avformat_free_context(pFormatCtxA_O);
	avformat_free_context(pFormatCtxV_O);

	return 0;

}