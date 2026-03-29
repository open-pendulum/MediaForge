#pragma once

#include <memory>
#include <string>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

class VideoEncoder {
public:
    using ProgressCallback = std::function<void(float)>;

    VideoEncoder();
    ~VideoEncoder();

    bool open(int width, int height, AVPixelFormat pixFmt, AVRational framerate,
               const std::string& encoderName = "auto", int64_t bitrate = 0);
    void close();

    bool sendFrame(AVFrame* frame);
    bool receivePacket(AVPacket* packet);
    void flush();

    int width() const { return codecCtx_ ? codecCtx_->width : 0; }
    int height() const { return codecCtx_ ? codecCtx_->height : 0; }
    AVPixelFormat pixFmt() const { return codecCtx_ ? codecCtx_->pix_fmt : AV_PIX_FMT_NONE; }
    AVCodecContext* getCodecContext() const { return codecCtx_; }

    void setProgressCallback(ProgressCallback callback) { onProgress_ = callback; }

private:
    bool tryOpenEncoder(const char* encoderName, AVDictionary** opts = nullptr);
    bool initSwsContext(int srcWidth, int srcHeight, AVPixelFormat srcPixFmt);

    AVCodecContext* codecCtx_ = nullptr;
    const AVCodec* codec_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVFrame* encFrame_ = nullptr;
    ProgressCallback onProgress_;
};