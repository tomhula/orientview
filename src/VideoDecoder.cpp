// Copyright � 2014 Mikko Ronkainen <firstname@mikkoronkainen.com>
// License: GPLv3, see the LICENSE file.

#include <windows.h>

#include <QtGlobal>

extern "C"
{
#define __STDC_CONSTANT_MACROS
#include <libavutil/imgutils.h>
}

#include "VideoDecoder.h"
#include "FrameData.h"

using namespace OrientView;

bool VideoDecoder::isRegistered = false;

namespace
{
	void ffmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl)
	{
		if (level <= AV_LOG_WARNING)
		{
			int print_prefix = 0;
			char line[1024] = { 0 };
			av_log_format_line(ptr, level, fmt, vl, line, 1024, &print_prefix);
			qWarning(line);
		}
	}

	bool openCodecContext(int* streamIndex, AVFormatContext* formatContext, AVMediaType mediaType)
	{
		*streamIndex = av_find_best_stream(formatContext, mediaType, -1, -1, nullptr, 0);

		if (*streamIndex < 0)
		{
			qWarning("Could not find %s stream in input file", av_get_media_type_string(mediaType));
			return false;
		}
		else
		{
			AVCodecContext* codecContext = formatContext->streams[*streamIndex]->codec;
			AVCodec* codec = avcodec_find_decoder(codecContext->codec_id);

			if (!codec)
			{
				qWarning("Could not find %s codec", av_get_media_type_string(mediaType));
				return false;
			}

			AVDictionary* opts = nullptr;

			if (avcodec_open2(codecContext, codec, &opts) < 0)
			{
				qWarning("Could not open %s codec", av_get_media_type_string(mediaType));
				return false;
			}
		}

		return true;
	}
}

VideoDecoder::VideoDecoder()
{
	for (int i = 0; i < 8; ++i)
	{
		resizedPicture.data[i] = nullptr;
		resizedPicture.linesize[i] = 0;
	}
}

bool VideoDecoder::initialize(const QString& fileName)
{
	qDebug("Initializing VideoDecoder (%s)", fileName.toLocal8Bit().constData());

	if (!isRegistered)
	{
		av_log_set_callback(ffmpegLogCallback);
		av_register_all();

		isRegistered = true;
	}

	try
	{
		if (avformat_open_input(&formatContext, fileName.toLocal8Bit().constData(), nullptr, nullptr) < 0)
			throw std::runtime_error("Could not open source file");

		if (avformat_find_stream_info(formatContext, nullptr) < 0)
			throw std::runtime_error("Could not find stream information");

		if (!openCodecContext(&videoStreamIndex, formatContext, AVMEDIA_TYPE_VIDEO))
			throw std::runtime_error("Could not open video codec context");

		videoStream = formatContext->streams[videoStreamIndex];
		videoCodecContext = videoStream->codec;
		videoInfo.frameWidth = videoCodecContext->width;
		videoInfo.frameHeight = videoCodecContext->height;

		frame = av_frame_alloc();

		if (!frame)
			throw std::runtime_error("Could not allocate frame");

		av_init_packet(&packet);
		packet.data = nullptr;
		packet.size = 0;

		resizeContext = sws_getContext(videoInfo.frameWidth, videoInfo.frameHeight, videoCodecContext->pix_fmt, videoInfo.frameWidth, videoInfo.frameHeight, PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);

		if (!resizeContext)
			throw std::runtime_error("Could not get resize context");

		if (avpicture_alloc(&resizedPicture, PIX_FMT_RGBA, videoInfo.frameWidth, videoInfo.frameHeight) < 0)
			throw std::runtime_error("Could not allocate picture");

		videoInfo.frameDataLength = videoInfo.frameHeight * resizedPicture.linesize[0];
		videoInfo.totalFrameCount = videoStream->nb_frames;
		videoInfo.averageFrameDuration = (int)av_rescale(1000000, videoStream->avg_frame_rate.den, videoStream->avg_frame_rate.num);
		videoInfo.averageFrameRateNum = videoStream->avg_frame_rate.num;
		videoInfo.averageFrameRateDen = videoStream->avg_frame_rate.den;
		videoInfo.averageFrameRate = (double)videoStream->avg_frame_rate.num / videoStream->avg_frame_rate.den;
	}
	catch (const std::exception& ex)
	{
		qWarning("Could not initialize VideoDecoder: %s", ex.what());
		return false;
	}

	isInitialized = true;
	return true;
}

void VideoDecoder::shutdown()
{
	qDebug("Shutting down VideoDecoder");

	avcodec_close(videoCodecContext);
	avformat_close_input(&formatContext);
	av_frame_free(&frame);
	sws_freeContext(resizeContext);
	avpicture_free(&resizedPicture);

	formatContext = nullptr;
	videoCodecContext = nullptr;
	videoStream = nullptr;
	videoStreamIndex = -1;
	frame = nullptr;
	resizeContext = nullptr;
	lastFrameTimestamp = 0;
	videoInfo = VideoInfo();

	for (int i = 0; i < 8; ++i)
	{
		resizedPicture.data[i] = nullptr;
		resizedPicture.linesize[i] = 0;
	}

	isInitialized = false;
}

bool VideoDecoder::getNextFrame(FrameData* frameDataPtr)
{
	if (!isInitialized)
		return false;

	while (true)
	{
		if (av_read_frame(formatContext, &packet) >= 0)
		{
			if (packet.stream_index == videoStreamIndex)
			{
				int gotPicture = 0;
				int decodedBytes = avcodec_decode_video2(videoCodecContext, frame, &gotPicture, &packet);
				videoInfo.currentFrameNumber++;

				if (decodedBytes < 0)
				{
					qWarning("Could not decode video frame");
					av_free_packet(&packet);
					return false;
				}

				if (gotPicture)
				{
					sws_scale(resizeContext, frame->data, frame->linesize, 0, frame->height, resizedPicture.data, resizedPicture.linesize);

					frameDataPtr->data = resizedPicture.data[0];
					frameDataPtr->dataLength = frame->height * resizedPicture.linesize[0];
					frameDataPtr->rowLength = resizedPicture.linesize[0];
					frameDataPtr->width = videoCodecContext->width;
					frameDataPtr->height = frame->height;
					frameDataPtr->duration = (int)av_rescale((frame->best_effort_timestamp - lastFrameTimestamp) * 1000000, videoStream->time_base.num, videoStream->time_base.den);
					frameDataPtr->number = videoInfo.currentFrameNumber;

					lastFrameTimestamp = frame->best_effort_timestamp;

					if (frameDataPtr->duration < 0 || frameDataPtr->duration > 1000000)
						frameDataPtr->duration = videoInfo.averageFrameDuration;

					av_free_packet(&packet);
					return true;
				}
			}

			av_free_packet(&packet);
		}
		else
			return false;
	}
}

VideoInfo VideoDecoder::getVideoInfo() const
{
	return videoInfo;
}
