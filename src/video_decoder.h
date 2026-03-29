#pragma once

#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    bool open(AVCodecParameters* codecParams, bool allowHardware = true);
    void close();

    bool sendPacket(AVPacket* packet);
    bool receiveFrame(AVFrame* frame);
    void flush();

    int width() const { return codecCtx_ ? codecCtx_->width : 0; }
    int height() const { return codecCtx_ ? codecCtx_->height : 0; }
    AVPixelFormat pixFmt() const { return codecCtx_ ? codecCtx_->pix_fmt : AV_PIX_FMT_NONE; }
    AVRational framerate() const { return codecCtx_ ? codecCtx_->framerate : AVRational{0, 1}; }

private:
    AVCodecContext* codecCtx_ = nullptr;
    const AVCodec* codec_ = nullptr;
    int refCount_ = 0;
};