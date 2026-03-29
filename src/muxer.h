#pragma once

#include <string>
#include <memory>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class Muxer {
public:
    Muxer();
    ~Muxer();

    bool open(const std::string& outputPath);
    void close();

    bool writeHeader();
    bool writePacket(AVPacket* packet);
    bool writeTrailer();

    int addStream(AVCodecParameters* codecParams);
    int addStream(AVCodecContext* codecCtx);
    void setStreamTimeBase(int streamIndex, AVRational timeBase);

    AVFormatContext* getFormatContext() const { return fmtCtx_; }

private:
    AVFormatContext* fmtCtx_ = nullptr;
    bool headerWritten_ = false;
    std::vector<int64_t> lastDts_;
    std::vector<int64_t> lastPts_;
    std::vector<AVRational> codecTimeBases_;
};