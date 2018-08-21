/*
ffplay -f rawvideo -video_size 640*352 Test.yuv
如果不加_video_size 640*352 会出现
picture size 0x0 is invalid
*/
#include<stdio.h>
#include<Windows.h>
extern "C" {
#include"..\include\libavcodec\avcodec.h"
#include"..\include\libavformat\avformat.h"
#include"..\include\libswscale\swscale.h"
#include"..\include\libavutil\imgutils.h"
}
int main(int argc, char* argv[])
{
	//
	AVFormatContext *pFormatContext = nullptr;
	AVCodecContext *pCodecContex = nullptr;
	AVCodec *pCodec = nullptr;
	int iRe = 0; 
	//
	FILE *pOut = nullptr;
	fopen_s(&pOut, "Test.yuv", "wb+");

	//FFmpeg  Init
	av_register_all();
	//get AVFormatContext
	pFormatContext = avformat_alloc_context();
	//Open Input File
	iRe = avformat_open_input(&pFormatContext, "Test.mp4", nullptr, nullptr);
	if (iRe != 0) {
		printf("avformat_open_input faile\n");
	}
	//Find Stream info
	iRe = avformat_find_stream_info(pFormatContext, nullptr);
	if (iRe < 0) {
		printf("avformat_find_stream_info faile\n");
	}
	//Get Codec
	int nVtype = -1;
	iRe = av_find_best_stream(pFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
	if (iRe < 0)
	{
		printf("av_find_best_stream faile\n");
	}
	nVtype = iRe;

	pCodecContex = avcodec_alloc_context3(nullptr);

	iRe = avcodec_parameters_to_context(pCodecContex, pFormatContext->streams[nVtype]->codecpar);
	if (iRe < 0)
	{
		printf("avcodec_parameters_to_context faile\n");
	}
	iRe = avcodec_open2(pCodecContex, pCodec, nullptr);
	if (iRe !=0) {
		printf("avcodec_open2 faile\n");
	}
	//Get Packet
	AVPacket packet ;
	AVFrame *pFrame = nullptr;
	pFrame = av_frame_alloc();
	int got_picture_ptr = 0;
	//sws_scale
	AVFrame *pFrame_YUV = av_frame_alloc();

	SwsContext *pSwsContextYUV = sws_getContext(pCodecContex->width,
												pCodecContex->height,
												pCodecContex->pix_fmt,
												pCodecContex->width,
												pCodecContex->height,
												AV_PIX_FMT_YUV420P,
												SWS_BICUBIC,
												NULL,
												NULL,
												NULL);
	unsigned char *bufYUV = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecContex->width, pCodecContex->height, 1));
	av_image_fill_arrays(pFrame_YUV->data, pFrame_YUV->linesize, bufYUV, AV_PIX_FMT_YUV420P, pCodecContex->width, pCodecContex->height, 1);
//	avpicture_fill((AVPicture*)pFrame_YUV, bufYUV, AV_PIX_FMT_YUV420P, pCodecContex->width, pCodecContex->height);
	while (true) {
		iRe = av_read_frame(pFormatContext, &packet);
		if (iRe < 0) {
			break;
		}
		if (packet.stream_index == nVtype) {//Video

			iRe = avcodec_send_packet(pCodecContex, &packet);
			if (iRe < 0)
			{
				break;
			}
			while (avcodec_receive_frame(pCodecContex, pFrame) == 0)
			{
				int y_size = pCodecContex->width * pCodecContex->height;

				sws_scale(pSwsContextYUV,
					(const uint8_t* const *)pFrame->data,
					pFrame->linesize,
					0,
					pCodecContex->height,
					pFrame_YUV->data,
					pFrame_YUV->linesize);
				//
				fwrite(pFrame_YUV->data[0], 1, y_size, pOut);       //Y   
				fwrite(pFrame_YUV->data[1], 1, y_size / 4, pOut);   //U  
				fwrite(pFrame_YUV->data[2], 1, y_size / 4, pOut);   //V  
			}
			
		}
		av_packet_unref(&packet);
	}
	//
	printf("End\n");

	sws_freeContext(pSwsContextYUV);
	
	av_frame_free(&pFrame);
	avformat_free_context(pFormatContext);
	
	fclose(pOut);

	system("pause");

	return 0;
}

