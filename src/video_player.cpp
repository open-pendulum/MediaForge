#include "video_player.h"
#include <iostream>
#include <vector>
#define NOMINMAX
#include <windows.h>

extern "C" {
#include <libavutil/imgutils.h>
}

// Helper to convert UTF-8 to Wide String
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Helper to get Short Path for FFmpeg compatibility on Windows
static std::string GetShortPath(const std::string& utf8Path) {
    std::wstring widePath = Utf8ToWide(utf8Path);
    long length = GetShortPathNameW(widePath.c_str(), NULL, 0);
    if (length == 0) return utf8Path;

    std::vector<wchar_t> buffer(length);
    GetShortPathNameW(widePath.c_str(), &buffer[0], length);
    
    std::wstring shortWide(buffer.begin(), buffer.end() - 1);
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &shortWide[0], (int)shortWide.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &shortWide[0], (int)shortWide.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

VideoPlayer::VideoPlayer() {
    currentFrame = av_frame_alloc();
    rgbFrame = av_frame_alloc();
}

VideoPlayer::~VideoPlayer() {
    cleanup();
    if (currentFrame) av_frame_free(&currentFrame);
    if (rgbFrame) av_frame_free(&rgbFrame);
    if (rgbBuffer) av_free(rgbBuffer);
}

void VideoPlayer::cleanup() {
    stop();
    
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    if (codecContext) {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }
    if (formatContext) {
        avformat_close_input(&formatContext);
        formatContext = nullptr;
    }
    
    videoStreamIndex = -1;
    duration = 0.0;
    currentTime = 0.0;
}

bool VideoPlayer::open(const std::string& path) {
    cleanup();
    
    std::string pathForFFmpeg = GetShortPath(path);
    std::cout << "Opening video: " << pathForFFmpeg << std::endl;
    
    if (avformat_open_input(&formatContext, pathForFFmpeg.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open video file: " << path << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "Could not find stream info" << std::endl;
        cleanup();
        return false;
    }
    
    // Find video stream
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }
    
    if (videoStreamIndex == -1) {
        std::cerr << "No video stream found" << std::endl;
        cleanup();
        return false;
    }
    
    if (!initDecoder()) {
        cleanup();
        return false;
    }
    
    // Get duration
    if (formatContext->duration != AV_NOPTS_VALUE) {
        duration = (double)formatContext->duration / AV_TIME_BASE;
    }
    
    // Get FPS
    AVRational frameRate = formatContext->streams[videoStreamIndex]->avg_frame_rate;
    if (frameRate.num > 0 && frameRate.den > 0) {
        fps = av_q2d(frameRate);
    }
    
    std::cout << "Video opened: " << getWidth() << "x" << getHeight() 
              << " @ " << fps << " fps, duration: " << duration << "s" << std::endl;
    
    return true;
}

bool VideoPlayer::initDecoder() {
    AVStream* videoStream = formatContext->streams[videoStreamIndex];
    
    const AVCodec* decoder = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!decoder) {
        std::cerr << "Failed to find decoder" << std::endl;
        return false;
    }
    
    codecContext = avcodec_alloc_context3(decoder);
    if (!codecContext) {
        std::cerr << "Failed to allocate codec context" << std::endl;
        return false;
    }
    
    if (avcodec_parameters_to_context(codecContext, videoStream->codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters" << std::endl;
        return false;
    }
    
    if (avcodec_open2(codecContext, decoder, nullptr) < 0) {
        std::cerr << "Failed to open decoder" << std::endl;
        return false;
    }
    
    // Initialize SwsContext for YUV to RGB conversion
    swsContext = sws_getContext(
        codecContext->width, codecContext->height, codecContext->pix_fmt,
        codecContext->width, codecContext->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!swsContext) {
        std::cerr << "Could not initialize sws context" << std::endl;
        return false;
    }
    
    // Allocate RGB buffer
    rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
    rgbBuffer = (uint8_t*)av_malloc(rgbBufferSize);
    
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer,
                        AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
    
    return true;
}

void VideoPlayer::close() {
    cleanup();
}

void VideoPlayer::play() {
    playing = true;
    paused = false;
}

void VideoPlayer::pause() {
    paused = true;
}

void VideoPlayer::stop() {
    playing = false;
    paused = false;
    currentTime = 0.0;
}

bool VideoPlayer::seekTo(double timeSeconds) {
    if (!formatContext || videoStreamIndex == -1) return false;
    
    int64_t timestamp = (int64_t)(timeSeconds * AV_TIME_BASE);
    
    if (av_seek_frame(formatContext, -1, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        std::cerr << "Seek failed" << std::endl;
        return false;
    }
    
    avcodec_flush_buffers(codecContext);
    currentTime = timeSeconds;
    
    // Decode to the exact frame
    return decodeNextFrame();
}

bool VideoPlayer::decodeNextFrame() {
    if (!formatContext || !codecContext) return false;
    
    std::lock_guard<std::mutex> lock(frameMutex);
    
    AVPacket* packet = av_packet_alloc();
    bool frameDecoded = false;
    
    while (av_read_frame(formatContext, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            if (avcodec_send_packet(codecContext, packet) >= 0) {
                if (avcodec_receive_frame(codecContext, currentFrame) >= 0) {
                    // Update current time based on frame PTS
                    if (currentFrame->pts != AV_NOPTS_VALUE) {
                        AVRational timeBase = formatContext->streams[videoStreamIndex]->time_base;
                        currentTime = currentFrame->pts * av_q2d(timeBase);
                    }
                    frameDecoded = true;
                    av_packet_unref(packet);
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }
    
    av_packet_free(&packet);
    return frameDecoded;
}

AVFrame* VideoPlayer::getCurrentFrame() {
    return currentFrame;
}

bool VideoPlayer::getRGBFrame(uint8_t** rgbData, int* width, int* height) {
    if (!currentFrame || !swsContext) return false;
    
    std::lock_guard<std::mutex> lock(frameMutex);
    
    // Convert YUV to RGB
    sws_scale(swsContext,
              currentFrame->data, currentFrame->linesize, 0, codecContext->height,
              rgbFrame->data, rgbFrame->linesize);
    
    *rgbData = rgbBuffer;
    *width = codecContext->width;
    *height = codecContext->height;
    
    return true;
}

double VideoPlayer::getDuration() const {
    return duration;
}

double VideoPlayer::getCurrentTime() const {
    return currentTime;
}

int VideoPlayer::getWidth() const {
    return codecContext ? codecContext->width : 0;
}

int VideoPlayer::getHeight() const {
    return codecContext ? codecContext->height : 0;
}

double VideoPlayer::getFPS() const {
    return fps;
}
