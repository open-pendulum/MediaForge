#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

class Demuxer {
public:
    struct StreamInfo {
        int streamIndex = -1;
        AVMediaType codecType = AVMEDIA_TYPE_UNKNOWN;
        AVCodecParameters* codecParams = nullptr;
        AVRational timeBase;
        int64_t duration = 0;
    };

    Demuxer();
    ~Demuxer();

    bool open(const std::string& inputPath);
    void close();

    bool readPacket(AVPacket* packet);
    bool seek(int streamIndex, int64_t timestamp, int flags = 0);

    const std::vector<StreamInfo>& getStreams() const { return streams_; }
    int getVideoStreamIndex() const { return videoStreamIndex_; }
    int getAudioStreamIndex() const { return audioStreamIndex_; }
    AVFormatContext* getFormatContext() const { return fmtCtx_; }
    int64_t getDuration() const { return fmtCtx_ ? fmtCtx_->duration : 0; }

private:
    AVFormatContext* fmtCtx_ = nullptr;
    std::vector<StreamInfo> streams_;
    int videoStreamIndex_ = -1;
    int audioStreamIndex_ = -1;
};