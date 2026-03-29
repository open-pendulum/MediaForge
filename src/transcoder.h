#pragma once

#include <string>
#include <memory>
#include <functional>

#include "demuxer.h"
#include "video_decoder.h"
#include "video_encoder.h"
#include "muxer.h"

class Transcoder {
public:
    Transcoder();
    ~Transcoder();

    bool run(const std::string& inputPath, const std::string& outputPath, const std::string& encoderName = "auto", bool allowHardwareDecoders = true);
    static bool isHevc(const std::string& inputPath);

    void setPauseCallback(std::function<bool()> cb);
    void setProgressCallback(std::function<void(float)> callback);

private:
    std::function<bool()> pauseCallback;
    std::function<void(float)> onProgress;

    Demuxer demuxer_;
    VideoDecoder videoDecoder_;
    VideoEncoder videoEncoder_;
    Muxer muxer_;

    int videoStreamIndex_ = -1;
    int audioStreamIndex_ = -1;

    int videoOutStreamIndex_ = -1;
    int audioOutStreamIndex_ = -1;

    bool initVideo(const std::string& encoderName, bool allowHardwareDecoders);
    bool initAudio();
    bool process();
};