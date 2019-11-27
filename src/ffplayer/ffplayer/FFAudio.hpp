#pragma once

#include <ryulib/Worker.hpp>
#include <ryulib/sdl_audio.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

class FFAudio {
public:
	FFAudio()
	{
		frame = av_frame_alloc();
		reframe = av_frame_alloc();

		worker_.setOnTask([&](int task, const string text, const void* data, int size, int tag){
			decode_and_play((AVPacket*) data);
		});
	}

	bool open(AVFormatContext* context)
	{
		stream_index_ = -1;
		for (int i = 0; i < context->nb_streams; i++)
			if (context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
				stream_index_ = i;
				break;
			}
		if (stream_index_ == -1) {
			printf("FFAudio.open - stream_ == -1 \n");
			return false;
		}

		parameters_ = context->streams[stream_index_]->codecpar;
		codec_ = avcodec_find_decoder(parameters_->codec_id);
		if (codec_ == NULL) {
			printf("FFAudio.open - codec == NULL \n");
			return false;
		}

		context_ = avcodec_alloc_context3(codec_);
		if (avcodec_parameters_to_context(context_, parameters_) != 0) 
		{
			printf("FFAudio.open - avcodec_parameters_to_context \n");
			return false;
		}

		if (avcodec_open2(context_, codec_, NULL) < 0) {
			printf("FFAudio.open - avcodec_open2 \n");
			return false;
		}

		swr_ = swr_alloc_set_opts(
			NULL,
			context_->channel_layout,
			AV_SAMPLE_FMT_FLT,
			context_->sample_rate,
			context_->channel_layout,
			(AVSampleFormat) parameters_->format,
			context_->sample_rate,
			0,
			NULL
		);
		swr_init(swr_);

		return audio_.open(context_->channels, context_->sample_rate, 1024);
	}

	void close()
	{

	}

	void write(AVPacket* packet)
	{
		worker_.add(0, packet);
	}

	int getStreamIndex() { return stream_index_; }

	bool isEmpty() { return audio_.getDelayCount() < 2; }

private:
	int stream_index_ = -1;
	AVCodecParameters* parameters_ = nullptr;
	AVCodecContext* context_ = nullptr;
	AVCodec* codec_ = nullptr;
	Worker worker_;
	AudioSDL audio_;
	SwrContext* swr_;
	AVFrame* frame = nullptr;
	AVFrame* reframe = nullptr;

	void decode_and_play(AVPacket* packet)
	{
		int ret = avcodec_send_packet(context_, packet) < 0;
		if (ret < 0) {
			printf("FFAudio - Error sending a packet for decoding \n");
			return;
		}	

		while (ret >= 0) {
			ret = avcodec_receive_frame(context_, frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			} else if (ret < 0) {
				printf("Error sending a packet for decoding \n");
				return;
			}

			// ���� ��ȯ
			reframe->channel_layout = frame->channel_layout;
			reframe->sample_rate = frame->sample_rate;
			reframe->format = AV_SAMPLE_FMT_FLT;
			int ret = swr_convert_frame(swr_, reframe, frame);

			int data_size = av_samples_get_buffer_size(NULL, context_->channels, frame->nb_samples, (AVSampleFormat) reframe->format, 0);
			audio_.play(reframe->data[0], data_size);
		}

		av_packet_free(&packet);
	}
};