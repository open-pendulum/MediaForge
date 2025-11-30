#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    // File operations
    bool open(const std::string& path);
    void close();

    // Playback control
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return playing; }
    bool isPaused() const { return paused; }

    // Seeking
    bool seekTo(double timeSeconds);

    // Frame access
    bool decodeNextFrame();
    AVFrame* getCurrentFrame();
    
    // Video info
    double getDuration() const;
    double getCurrentTime() const;
    int getWidth() const;
    int getHeight() const;
    double getFPS() const;
    
    // Get RGB frame for rendering (converted from YUV)
    bool getRGBFrame(uint8_t** rgbData, int* width, int* height);

private:
    void cleanup();
    bool initDecoder();
    
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
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
