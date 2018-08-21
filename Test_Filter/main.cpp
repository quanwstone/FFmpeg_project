/*
ffplay.exe -f rawvideo -video_size 512x288 test.yuv
*/
#include<stdio.h>
#include<Windows.h>
extern "C"
{
#include"libavformat\avformat.h"
#include"libavcodec\avcodec.h"
#include"libavfilter\avfiltergraph.h"
#include"libavfilter\buffersink.h"
#include"libavfilter\buffersrc.h"
#include"libavutil\opt.h"
}

//const char *str_filter = "scale=iw*2:ih*2 ";//缩放
//const char *str_filter = "movie=logo.png[wm];[in][wm]overlay=5:5[out]";//添加logo
//const char *str_filter = "drawgrid = w = iw / 3:h = ih / 3 : t = 2 : c = white@0.5";//绘制网格
//const char *str_filter = "drawtext=fontsize = 30:fontfile = simkai.ttf : text = 'hello world' : x = (w - text_w) / 2 : y = (h - text_h) / 2";//绘制字体
const char *str_filter = "edgedetect = low = 0.1:high = 0.4";//描边

static void write_frame(const AVFrame *frame,FILE *file_fd)
{
	static int printf_flag = 0;
	if (!printf_flag) {
		printf_flag = 1;
		printf("frame widht=%d,frame height=%d\n", frame->width, frame->height);

		if (frame->format == AV_PIX_FMT_YUV420P) {
			printf("format is yuv420p\n");
		}
		else {
			printf("formet is = %d \n", frame->format);
		}

	}

	fwrite(frame->data[0], 1, frame->width*frame->height, file_fd);
	fwrite(frame->data[1], 1, frame->width / 2 * frame->height / 2, file_fd);
	fwrite(frame->data[2], 1, frame->width / 2 * frame->height / 2, file_fd);
}

int main(int argc, char *argv[])
{
	FILE *pTest = nullptr;

	errno_t er = fopen_s(&pTest, "./test.yuv", "wb+");
	if (er != 0)
	{
		printf("fopen_s faile\n");
	}
	av_register_all();
	avfilter_register_all();

	AVFormatContext *pForamtContext = nullptr;
	AVCodec *pCodec = nullptr;
	AVCodecContext *pCodecContex = nullptr;

	int iVideo_stream_Index = 0;

	pForamtContext = avformat_alloc_context();

	int ir = avformat_open_input(&pForamtContext, "cuc_ieschool.flv", nullptr, nullptr);
	if (ir != 0)
	{
		printf("avformat_open_input faile\n");
	}
	ir = avformat_find_stream_info(pForamtContext, nullptr);
	if (ir < 0)
	{
		printf("avformat_find_stream_info faile\n");
	}
	ir = av_find_best_stream(pForamtContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
	if (ir < 0)
	{
		printf("av_find_best_stream faile\n");
	}
	iVideo_stream_Index = ir;

	pCodecContex = avcodec_alloc_context3(nullptr);

	ir = avcodec_parameters_to_context(pCodecContex, pForamtContext->streams[iVideo_stream_Index]->codecpar);
	if (ir < 0)
	{
		printf("avcodec_parameters_to_context faile\n");
	}

	av_opt_set_int(pCodecContex, "refcounted_frames", 1, 0);

	ir = avcodec_open2(pCodecContex, pCodec, nullptr);
	if (ir < 0)
	{
		printf("avcodec_open2 faile\n");
	}

	AVFilter *buffersrc = avfilter_get_by_name("buffer");
	AVFilter *buffersink = avfilter_get_by_name("buffersink");
	AVFilterInOut *outputs = avfilter_inout_alloc();
	AVFilterInOut *inputs = avfilter_inout_alloc();

	AVRational time_base = pForamtContext->streams[iVideo_stream_Index]->time_base;

	enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P,AV_PIX_FMT_NONE};

	AVFilterGraph *pFilterGraph = nullptr;

	pFilterGraph = avfilter_graph_alloc();

	AVFilterContext *buffersink_ctx;
	AVFilterContext *buffersrc_ctx;
	char args[512] = {0};
	snprintf(args, sizeof(args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		pCodecContex->width, pCodecContex->height, pCodecContex->pix_fmt,
		time_base.num, time_base.den,
		pCodecContex->sample_aspect_ratio.num, pCodecContex->sample_aspect_ratio.den);

	ir = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, nullptr, pFilterGraph);
	if (ir < 0)
	{
		printf("avfilter_graph_create_filter faile\n");
	}
	ir = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", nullptr, nullptr, pFilterGraph);
	if (ir < 0)
	{
		printf("avfilter_graph_create_filter faile\n");
	}
	ir = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (ir < 0)
	{
		printf("av_opt_set_int_list faile\n");
	}

	outputs->name = av_strdup("in");
	outputs->filter_ctx = buffersrc_ctx;
	outputs->pad_idx = 0;
	outputs->next = NULL;

	inputs->name = av_strdup("out");
	inputs->filter_ctx = buffersink_ctx;
	inputs->pad_idx = 0;
	inputs->next = NULL;

	/* Add a graph described by a string to a graph */
	if ((ir = avfilter_graph_parse_ptr(pFilterGraph, str_filter,&inputs, &outputs, NULL)) < 0)
		printf("avfilter_graph_parse_ptr faile\n");

	/* Check validity and configure all the links and formats in the graph */
	if ((ir = avfilter_graph_config(pFilterGraph, NULL)) < 0)
		printf("avfilter_graph_config faile\n");

	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);

	AVPacket *packet = nullptr;
	packet = av_packet_alloc();
	AVFrame *pFrame = nullptr;
	pFrame = av_frame_alloc();
	AVFrame *filt_frame = nullptr;
	filt_frame = av_frame_alloc();

	int got_picture_ptr = 0;
	while (true)
	{
		ir = av_read_frame(pForamtContext, packet);
		if (ir < 0)
		{
			break;
		}
		if (packet->stream_index == iVideo_stream_Index)
		{
			ir = avcodec_send_packet(pCodecContex, packet);
			if (ir != 0)
			{
				printf("avcodec_send_packet faile\n");
				continue;
			}
			while (avcodec_receive_frame(pCodecContex, pFrame) == 0)
			{
				pFrame->pts = av_frame_get_best_effort_timestamp(pFrame);
				if (av_buffersrc_add_frame_flags(buffersrc_ctx, pFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
				{
					printf("av_buffersrc_add_frame_flags faile\n");
				}
				while (true)
				{
					ir = av_buffersink_get_frame(buffersink_ctx, filt_frame);
					if (ir == AVERROR(EAGAIN) || ir == AVERROR_EOF)
						break;

					write_frame(filt_frame, pTest);

					av_frame_unref(filt_frame);
				}
				av_frame_unref(pFrame);
			}
			//break;
		}

		av_packet_unref(packet);

	}
	printf("End\n");
	avfilter_graph_free(&pFilterGraph);
	avcodec_close(pCodecContex);
	avformat_close_input(&pForamtContext);
	av_frame_free(&pFrame);
	av_frame_free(&filt_frame);
	fclose(pTest);

	return 0;
}