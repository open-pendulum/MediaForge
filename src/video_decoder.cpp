#include "video_decoder.h"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
}

VideoDecoder::VideoDecoder() {}

VideoDecoder::~VideoDecoder() {
    close();
}

bool VideoDecoder::open(AVCodecParameters* codecParams, bool allowHardware) {
    close();

    AVCodecID codecId = codecParams->codec_id;

    if (allowHardware) {
        if (codecId == AV_CODEC_ID_H264) {
            if (avcodec_find_decoder_by_name("h264_cuvid")) {
                codec_ = avcodec_find_decoder_by_name("h264_cuvid");
            } else if (avcodec_find_decoder_by_name("h264_qsv")) {
                codec_ = avcodec_find_decoder_by_name("h264_qsv");
            }
        } else if (codecId == AV_CODEC_ID_HEVC) {
            if (avcodec_find_decoder_by_name("hevc_cuvid")) {
                codec_ = avcodec_find_decoder_by_name("hevc_cuvid");
            } else if (avcodec_find_decoder_by_name("hevc_qsv")) {
                codec_ = avcodec_find_decoder_by_name("hevc_qsv");
            }
        }
    }

    if (!codec_) {
        codec_ = avcodec_find_decoder(codecId);
    }

    if (!codec_) {
        std::cerr << "[VideoDecoder] Failed to find decoder for codec: " << codecId << std::endl;
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) {
        std::cerr << "[VideoDecoder] Failed to allocate codec context" << std::endl;
        return false;
    }

    avcodec_parameters_to_context(codecCtx_, codecParams);

    if (avcodec_open2(codecCtx_, codec_, nullptr) < 0) {
        std::cerr << "[VideoDecoder] Failed to open decoder: " << codec_->name << std::endl;
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
        return false;
    }

    std::cout << "[VideoDecoder] Opened decoder: " << codec_->name
              << " (" << codecCtx_->width << "x" << codecCtx_->height << ")" << std::endl;

    return true;
}

void VideoDecoder::close() {
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    codec_ = nullptr;
}

bool VideoDecoder::sendPacket(AVPacket* packet) {
    if (!codecCtx_) return false;
    return avcodec_send_packet(codecCtx_, packet) >= 0;
}

bool VideoDecoder::receiveFrame(AVFrame* frame) {
    if (!codecCtx_) return false;
    int ret = avcodec_receive_frame(codecCtx_, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return false;
    }
    if (ret < 0) {
        std::cerr << "[VideoDecoder] Decode error: " << ret << std::endl;
        return false;
    }
    return true;
}

void VideoDecoder::flush() {
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_);
    }
}