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
	FILE *p264 = nullptr;
	fopen_s(&p264,"Test.h264","wb+");
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
	//
	AVBitStreamFilterContext *pbsfc = av_bitstream_filter_init("h264_mp4toannexb");
	//Get Packet
	AVPacket packet ;
	AVFrame *pFrame = nullptr;
	pFrame = av_frame_alloc();
	int got_picture_ptr = 0;
	int frame_index = 0;

	while (true) {
		iRe = av_read_frame(pFormatContext, &packet);
		if (iRe < 0) {
			break;
		}
		if (packet.stream_index == nVtype) {//Video
			
			av_bitstream_filter_filter(pbsfc, pCodecContex, NULL, &packet.data, &packet.size, packet.data, packet.size, 0);
			
			fwrite(packet.data, 1, packet.size, p264);
		}
		av_free_packet(&packet);
	}
	//
	printf("End\n");
	av_bitstream_filter_close(pbsfc);
	av_frame_free(&pFrame);
	avformat_free_context(pFormatContext);
	
	fclose(p264);

	system("pause");
	return 0;
}

