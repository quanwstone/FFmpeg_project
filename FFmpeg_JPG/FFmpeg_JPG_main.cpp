#include<stdio.h>
#include<Windows.h>
extern "C" {
#include"..\include\libavcodec\avcodec.h"
#include"..\include\libavformat\avformat.h"
#include"..\include\libswscale\swscale.h"
}
/*
通过FFmpeg读取数据
通过SDL进行绘制
*/
bool WriteJPG(AVFrame *pFrame,int with,int Higth)
{
	AVFormatContext *pFormatCtx;
	AVCodecContext  *pCodecCtx;
	AVCodec			*pCodec;
	AVPacket		packet;

	int nhigh = Higth;
	int nWith = with;
	
	pFormatCtx = avformat_alloc_context();

	pFormatCtx->oformat = av_guess_format("mjpeg", NULL, NULL);

	// 创建并初始化一个和该url相关的AVIOContext
	if (avio_open(&pFormatCtx->pb, "D:\\Test.jpg", AVIO_FLAG_READ_WRITE) < 0) {
		return -1;
	}

	AVStream *pstream = avformat_new_stream(pFormatCtx, NULL);

	pCodecCtx = avcodec_alloc_context3(nullptr);
	
	int ir = avcodec_parameters_to_context(pCodecCtx, pstream->codecpar);
	if (ir < 0)
	{
		printf("avcodec_parameters_to_context faile\n");
	}
	//CodecContex 赋值
	pCodecCtx->codec_id = pFormatCtx->oformat->video_codec;
	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
	pCodecCtx->width = nWith;
	pCodecCtx->height = nhigh;
	pCodecCtx->time_base.den = 30;
	pCodecCtx->time_base.num = 1;

	// 查找编码器
	pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
	if (!pCodec) {
		printf("avcodec_find_encoder faile\n");
		return -1;
	}
	// 设置pCodecCtx的编码器为pCodec
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("avcodec_open2 faile\n");
		return -1;
	}

	//Write Header
	avformat_write_header(pFormatCtx, NULL);

	ir = av_new_packet(&packet, nhigh*nWith * 3);
	if (ir != 0)
	{
		printf("av_new_packet faile\n");
	}
	ir = avcodec_send_frame(pCodecCtx, pFrame);
	if (ir == 0)
	{
		while (avcodec_receive_packet(pCodecCtx, &packet) == 0)
		{
			av_write_frame(pFormatCtx, &packet);
		}
	}

	//Write Trailer
	av_write_trailer(pFormatCtx);

	avio_close(pFormatCtx->pb);
	avformat_free_context(pFormatCtx);

	return true;
}
int main(int argc, char* argv[])
{
	//FFmpeg  
	AVFormatContext *pFormatCtx;
	int             i, videoindex, frameFinished;
	AVCodecContext  *pCodecCtx;
	AVCodec         *pCodec;
	AVFrame *pFrame, *pFrameYUV;
	AVPacket packet;
	//
	FILE *fp_yuv;
	int ret, got_picture;
	char filepath[] = "Test.mp4";
	//
	av_register_all();

	pFormatCtx = avformat_alloc_context();

	if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}
	if (avformat_find_stream_info(pFormatCtx, NULL)<0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}
	videoindex = -1;
	
	ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
	if (ret < 0)
	{
		printf("av_find_best_stream faile\n");
	}
	videoindex = ret;

	pCodecCtx = avcodec_alloc_context3(nullptr);

	ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar);
	if (ret < 0)
	{
		printf("avcodec_parameters_to_context faile\n");
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL)<0) {
		printf("Could not open codec.\n");
		return -1;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	
	int ir = 0,iFlag = 0;
	while (av_read_frame(pFormatCtx, &packet) >= 0) {
		// Is this a packet from the video stream?  
		if (packet.stream_index == videoindex) {

			ir = avcodec_send_packet(pCodecCtx, &packet);
			if (ir < 0)
			{
				break;
			}
			while (avcodec_receive_frame(pCodecCtx, pFrame) == 0)
			{
				WriteJPG(pFrame, pCodecCtx->width, pCodecCtx->height);
				iFlag = 1;
				break;
			}
			if (iFlag)
			{
				break;
			}
		}
		av_packet_unref(&packet);
	}
	
	av_free(pFrame);
	av_free(pFrameYUV);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);

	system("pause");

	return 0;
}