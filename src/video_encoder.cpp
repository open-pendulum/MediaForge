#include "video_encoder.h"
#include <iostream>
#include <algorithm>

extern "C" {
#include <libavutil/opt.h>
}

static int64_t calculateRecommendedBitrate(int width, int height, double fps) {
    int64_t pixels = (int64_t)width * height;

    struct ResolutionPreset {
        int64_t pixels;
        int64_t bitrate_30fps;
    };

    ResolutionPreset presets[] = {
        {1280 * 720,    2000000},
        {1920 * 1080,   4000000},
        {2560 * 1440,   7500000},
        {3840 * 2160,   15000000},
    };

    int64_t baseBitrate = 0;

    if (pixels <= presets[0].pixels) {
        baseBitrate = (int64_t)(presets[0].bitrate_30fps * ((double)pixels / presets[0].pixels));
    } else if (pixels >= presets[3].pixels) {
        baseBitrate = (int64_t)(presets[3].bitrate_30fps * ((double)pixels / presets[3].pixels));
    } else {
        for (size_t i = 0; i < 3; i++) {
            if (pixels >= presets[i].pixels && pixels <= presets[i + 1].pixels) {
                double ratio = (double)(pixels - presets[i].pixels) /
                               (double)(presets[i + 1].pixels - presets[i].pixels);
                baseBitrate = (int64_t)(presets[i].bitrate_30fps +
                             ratio * (presets[i + 1].bitrate_30fps - presets[i].bitrate_30fps));
                break;
            }
        }
    }

    double fpsRatio = std::max(0.5, std::min(2.5, fps / 30.0));
    baseBitrate = (int64_t)(baseBitrate * fpsRatio);

    return baseBitrate;
}

VideoEncoder::VideoEncoder() {}

VideoEncoder::~VideoEncoder() {
    close();
}

bool VideoEncoder::initSwsContext(int srcWidth, int srcHeight, AVPixelFormat srcPixFmt) {
    if (codecCtx_->pix_fmt == srcPixFmt &&
        codecCtx_->width == srcWidth &&
        codecCtx_->height == srcHeight) {
        return true;
    }

    swsCtx_ = sws_getContext(
        srcWidth, srcHeight, srcPixFmt,
        codecCtx_->width, codecCtx_->height, codecCtx_->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr
    );

    if (!swsCtx_) {
        std::cerr << "[VideoEncoder] Failed to create SwsContext for conversion" << std::endl;
        return false;
    }

    encFrame_ = av_frame_alloc();
    encFrame_->format = codecCtx_->pix_fmt;
    encFrame_->width = codecCtx_->width;
    encFrame_->height = codecCtx_->height;
    if (av_frame_get_buffer(encFrame_, 32) < 0) {
        std::cerr << "[VideoEncoder] Failed to allocate conversion buffer" << std::endl;
        return false;
    }

    return true;
}

bool VideoEncoder::tryOpenEncoder(const char* encoderName, AVDictionary** opts) {
    const AVCodec* encoder = avcodec_find_encoder_by_name(encoderName);
    if (!encoder) return false;

    AVCodecContext* tempCtx = avcodec_alloc_context3(encoder);
    if (!tempCtx) return false;

    tempCtx->height = codecCtx_->height;
    tempCtx->width = codecCtx_->width;
    tempCtx->sample_aspect_ratio = codecCtx_->sample_aspect_ratio;
    tempCtx->pix_fmt = encoder->pix_fmts ? encoder->pix_fmts[0] : AV_PIX_FMT_YUV420P;
    tempCtx->framerate = codecCtx_->framerate;
    tempCtx->time_base = av_inv_q(codecCtx_->framerate);
    tempCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    double fps = av_q2d(codecCtx_->framerate);
    int64_t recommendedBitrate = calculateRecommendedBitrate(codecCtx_->width, codecCtx_->height, fps);

    if (codecCtx_->bit_rate > 0) {
        tempCtx->bit_rate = std::min(recommendedBitrate, (int64_t)(codecCtx_->bit_rate * 0.7));
    } else {
        tempCtx->bit_rate = recommendedBitrate;
    }

    double gop_fps = (tempCtx->framerate.num > 0) ? av_q2d(tempCtx->framerate) : 30.0;
    tempCtx->gop_size = (int)(gop_fps * 2.0);

    if (opts && *opts) {
        if (avcodec_open2(tempCtx, encoder, opts) < 0) {
            avcodec_free_context(&tempCtx);
            return false;
        }
    } else {
        AVDictionary* tmpOpts = nullptr;
        if (avcodec_open2(tempCtx, encoder, &tmpOpts) < 0) {
            avcodec_free_context(&tempCtx);
            return false;
        }
        av_dict_free(&tmpOpts);
    }

    avcodec_free_context(&codecCtx_);
    codecCtx_ = tempCtx;
    codec_ = encoder;
    return true;
}

bool VideoEncoder::open(int width, int height, AVPixelFormat pixFmt, AVRational framerate,
                        const std::string& encoderName, int64_t bitrate) {
    close();

    codecCtx_ = avcodec_alloc_context3(nullptr);
    if (!codecCtx_) return false;

    codecCtx_->width = width;
    codecCtx_->height = height;
    codecCtx_->pix_fmt = pixFmt;
    codecCtx_->framerate = framerate;
    codecCtx_->time_base = av_inv_q(framerate);
    if (bitrate > 0) codecCtx_->bit_rate = bitrate;

    codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    bool encoderOpened = false;

    if (encoderName != "auto") {
        if (tryOpenEncoder(encoderName.c_str())) {
            encoderOpened = true;
        }
    }

    if (!encoderOpened) {
        if (tryOpenEncoder("hevc_nvenc")) encoderOpened = true;
        else if (tryOpenEncoder("hevc_qsv")) encoderOpened = true;
        else if (tryOpenEncoder("hevc_amf")) encoderOpened = true;
        else if (tryOpenEncoder("libx265")) encoderOpened = true;
    }

    if (!encoderOpened) {
        std::cerr << "[VideoEncoder] Failed to open any H.265 encoder" << std::endl;
        close();
        return false;
    }

    if (!initSwsContext(width, height, pixFmt)) {
        close();
        return false;
    }

    std::cout << "[VideoEncoder] Opened: " << codec_->name
              << " (" << width << "x" << height << " @ " << av_q2d(framerate) << " fps)"
              << " -> (" << codecCtx_->width << "x" << codecCtx_->height << ")" << std::endl;

    return true;
}

void VideoEncoder::close() {
    if (encFrame_) {
        av_frame_free(&encFrame_);
        encFrame_ = nullptr;
    }
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    codec_ = nullptr;
}

bool VideoEncoder::sendFrame(AVFrame* frame) {
    if (!codecCtx_) return false;

    AVFrame* frameToSend = frame;
    if (frame && swsCtx_) {
        sws_scale(swsCtx_,
            (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height,
            encFrame_->data, encFrame_->linesize);
        encFrame_->pts = frame->pts;
        encFrame_->pict_type = AV_PICTURE_TYPE_NONE;
        frameToSend = encFrame_;
    }

    return avcodec_send_frame(codecCtx_, frameToSend) >= 0;
}

bool VideoEncoder::receivePacket(AVPacket* packet) {
    if (!codecCtx_) return false;
    int ret = avcodec_receive_packet(codecCtx_, packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return false;
    }
    if (ret < 0) {
        std::cerr << "[VideoEncoder] Encode error: " << ret << std::endl;
        return false;
    }
    return true;
}

void VideoEncoder::flush() {
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_);
    }
}