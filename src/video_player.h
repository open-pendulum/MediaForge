#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include "demuxer.h"
#include "video_decoder.h"

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool open(const std::string& path);
    void close();

    void play();
    void pause();
    void stop();
    bool isPlaying() const { return playing; }
    bool isPaused() const { return paused; }

    bool seekTo(double timeSeconds);

    bool decodeNextFrame();
    AVFrame* getCurrentFrame();

    double getDuration() const;
    double getCurrentTime() const;
    int getWidth() const;
    int getHeight() const;
    double getFPS() const;

    bool getRGBFrame(uint8_t** rgbData, int* width, int* height);

private:
    void cleanup();
    bool initSwsContext();

    Demuxer demuxer_;
    VideoDecoder decoder_;

    AVFrame* currentFrame = nullptr;
    AVFrame* rgbFrame = nullptr;
    SwsContext* swsContext = nullptr;

    int videoStreamIndex = -1;
    double duration = 0.0;
    double currentTime = 0.0;
    double fps = 30.0;

    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::mutex frameMutex;

    uint8_t* rgbBuffer = nullptr;
    int rgbBufferSize = 0;
};