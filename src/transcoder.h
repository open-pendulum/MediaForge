#pragma once

#include <string>
#include <memory>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

class Transcoder {
public:
    Transcoder();
    ~Transcoder();

    bool run(const std::string& inputPath, const std::string& outputPath, const std::string& encoderName = "auto");
    static bool isHevc(const std::string& inputPath);
    
    void setPauseCallback(std::function<bool()> cb);

private:
    std::function<bool()> pauseCallback;
    AVFormatContext* inputFormatContext = nullptr;
    AVFormatContext* outputFormatContext = nullptr;
    
    struct StreamContext {
        AVCodecContext* decCtx = nullptr;
        AVCodecContext* encCtx = nullptr;
        AVStream* inStream = nullptr;
        AVStream* outStream = nullptr;
        int streamIndex = -1;
        int64_t nextPts = 0;  // 用于生成正确的帧时间戳
    };

    StreamContext videoStreamCtx;
    StreamContext audioStreamCtx;  // Audio stream context

    bool openInput(const std::string& inputPath);
    bool openOutput(const std::string& outputPath);
    bool initVideoTranscoding(const std::string& encoderName);
    bool initAudioTranscoding();  // Initialize audio transcoding
    void cleanup();
    
    // Hardware acceleration helper functions
    bool tryOpenEncoder(const char* encoderName, AVDictionary** opts = nullptr);
    bool tryOpenDecoder(const char* decoderName);
    
    int encode(AVCodecContext *avctx, AVStream *stream, AVFrame *frame, AVFormatContext *fmt_ctx);

public:
    using ProgressCallback = std::function<void(float)>;
    void setProgressCallback(ProgressCallback callback) { onProgress = callback; }

private:
    ProgressCallback onProgress;
};
