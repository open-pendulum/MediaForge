#include "video_player.h"
#include <iostream>

extern "C" {
#include <libavutil/imgutils.h>
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

    decoder_.close();
    demuxer_.close();

    videoStreamIndex = -1;
    duration = 0.0;
    currentTime = 0.0;
}

bool VideoPlayer::open(const std::string& path) {
    cleanup();

    if (!demuxer_.open(path)) {
        return false;
    }

    videoStreamIndex = demuxer_.getVideoStreamIndex();
    if (videoStreamIndex == -1) {
        std::cerr << "No video stream found" << std::endl;
        cleanup();
        return false;
    }

    const auto& streams = demuxer_.getStreams();
    auto& streamInfo = streams[videoStreamIndex];

    if (!decoder_.open(streamInfo.codecParams)) {
        cleanup();
        return false;
    }

    duration = (double)demuxer_.getDuration() / AV_TIME_BASE;

    AVRational frameRate = decoder_.framerate();
    if (frameRate.num > 0 && frameRate.den > 0) {
        fps = av_q2d(frameRate);
    }

    initSwsContext();

    std::cout << "Video opened: " << getWidth() << "x" << getHeight()
              << " @ " << fps << " fps, duration: " << duration << "s" << std::endl;

    return true;
}

bool VideoPlayer::initSwsContext() {
    swsContext = sws_getContext(
        decoder_.width(), decoder_.height(), decoder_.pixFmt(),
        decoder_.width(), decoder_.height(), AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsContext) {
        std::cerr << "Could not initialize sws context" << std::endl;
        return false;
    }

    rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, decoder_.width(), decoder_.height(), 1);
    rgbBuffer = (uint8_t*)av_malloc(rgbBufferSize);

    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer,
                        AV_PIX_FMT_RGB24, decoder_.width(), decoder_.height(), 1);

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
    if (!demuxer_.getFormatContext() || videoStreamIndex == -1) return false;

    int64_t timestamp = (int64_t)(timeSeconds * AV_TIME_BASE);

    if (!demuxer_.seek(videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD)) {
        std::cerr << "Seek failed" << std::endl;
        return false;
    }

    decoder_.flush();
    currentTime = timeSeconds;

    return decodeNextFrame();
}

bool VideoPlayer::decodeNextFrame() {
    if (!demuxer_.getFormatContext() || !currentFrame) return false;

    std::lock_guard<std::mutex> lock(frameMutex);

    AVPacket* packet = av_packet_alloc();
    bool frameDecoded = false;

    while (demuxer_.readPacket(packet)) {
        if (packet->stream_index == videoStreamIndex) {
            if (decoder_.sendPacket(packet)) {
                if (decoder_.receiveFrame(currentFrame)) {
                    if (currentFrame->pts != AV_NOPTS_VALUE) {
                        AVRational timeBase = demuxer_.getStreams()[videoStreamIndex].timeBase;
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

    sws_scale(swsContext,
              currentFrame->data, currentFrame->linesize, 0, decoder_.height(),
              rgbFrame->data, rgbFrame->linesize);

    *rgbData = rgbBuffer;
    *width = decoder_.width();
    *height = decoder_.height();

    return true;
}

double VideoPlayer::getDuration() const {
    return duration;
}

double VideoPlayer::getCurrentTime() const {
    return currentTime;
}

int VideoPlayer::getWidth() const {
    return decoder_.width();
}

int VideoPlayer::getHeight() const {
    return decoder_.height();
}

double VideoPlayer::getFPS() const {
    return fps;
}