#include "transcoder.h"
#include <iostream>
#include <thread>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

Transcoder::Transcoder() {}

Transcoder::~Transcoder() {}

void Transcoder::setPauseCallback(std::function<bool()> cb) {
    pauseCallback = cb;
}

void Transcoder::setProgressCallback(std::function<void(float)> callback) {
    onProgress = callback;
}

bool Transcoder::initVideo(const std::string& encoderName, bool allowHardwareDecoders) {
    videoStreamIndex_ = demuxer_.getVideoStreamIndex();
    if (videoStreamIndex_ < 0) {
        std::cerr << "[Transcoder] No video stream found" << std::endl;
        return false;
    }

    const auto& streams = demuxer_.getStreams();
    const Demuxer::StreamInfo& videoStream = streams[videoStreamIndex_];

    if (!videoDecoder_.open(videoStream.codecParams, allowHardwareDecoders)) {
        std::cerr << "[Transcoder] Failed to open video decoder" << std::endl;
        return false;
    }

    if (!videoEncoder_.open(
            videoDecoder_.width(),
            videoDecoder_.height(),
            videoDecoder_.pixFmt(),
            videoDecoder_.framerate(),
            encoderName)) {
        std::cerr << "[Transcoder] Failed to open video encoder" << std::endl;
        return false;
    }

    videoOutStreamIndex_ = muxer_.addStream(videoEncoder_.getCodecContext());
    if (videoOutStreamIndex_ < 0) {
        std::cerr << "[Transcoder] Failed to add video stream to muxer" << std::endl;
        return false;
    }

    AVRational videoTimeBase = videoEncoder_.getCodecContext()->time_base;
    muxer_.setStreamTimeBase(videoOutStreamIndex_, videoTimeBase);
    std::cout << "[Transcoder] Video time_base: " << videoTimeBase.num << "/" << videoTimeBase.den << std::endl;

    return true;
}

bool Transcoder::initAudio() {
    audioStreamIndex_ = demuxer_.getAudioStreamIndex();
    if (audioStreamIndex_ < 0) {
        std::cout << "[Transcoder] No audio stream found" << std::endl;
        return true;
    }

    std::cout << "[Transcoder] Audio: skipping (video only output)" << std::endl;
    audioStreamIndex_ = -1;
    return true;
}

bool Transcoder::process() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int64_t nextVideoPts = 0;
    int64_t nextAudioPts = 0;

    int64_t totalDuration = demuxer_.getDuration();

    while (true) {
        if (pauseCallback) {
            while (pauseCallback()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (!demuxer_.readPacket(packet)) {
            break;
        }

        if (onProgress && totalDuration > 0) {
            int64_t currentPts = packet->pts;
            AVRational timeBase = demuxer_.getFormatContext()->streams[packet->stream_index]->time_base;
            int64_t currentTime = av_rescale_q(currentPts, timeBase, AV_TIME_BASE_Q);
            float progress = (float)currentTime / (float)totalDuration;
            if (progress >= 0.0f && progress <= 1.0f) {
                onProgress(progress);
            }
        }

        if (packet->stream_index == videoStreamIndex_) {
            if (videoDecoder_.sendPacket(packet) && videoDecoder_.receiveFrame(frame)) {
                do {
                    if (frame->pts == AV_NOPTS_VALUE) {
                        frame->pts = nextVideoPts++;
                    } else {
                        AVRational srcTimeBase = demuxer_.getFormatContext()->streams[videoStreamIndex_]->time_base;
                        AVRational dstTimeBase = videoEncoder_.getCodecContext()->time_base;
                        frame->pts = av_rescale_q(frame->pts, srcTimeBase, dstTimeBase);
                        nextVideoPts = frame->pts + 1;
                    }
                    frame->pict_type = AV_PICTURE_TYPE_NONE;

                    if (videoEncoder_.sendFrame(frame)) {
                        AVPacket* encPkt = av_packet_alloc();
                        while (videoEncoder_.receivePacket(encPkt)) {
                            encPkt->stream_index = videoOutStreamIndex_;
                            muxer_.writePacket(encPkt);
                        }
                        av_packet_free(&encPkt);
                    }
                } while (videoDecoder_.receiveFrame(frame));
            }
        } else if (packet->stream_index == audioStreamIndex_) {
            packet->stream_index = audioOutStreamIndex_;
            muxer_.writePacket(packet);
        }

        av_packet_unref(packet);
    }

    videoEncoder_.flush();
    AVPacket* encPkt = av_packet_alloc();
    while (videoEncoder_.receivePacket(encPkt)) {
        encPkt->stream_index = videoOutStreamIndex_;
        muxer_.writePacket(encPkt);
    }
    av_packet_free(&encPkt);

    av_packet_free(&packet);
    av_frame_free(&frame);

    return true;
}

bool Transcoder::run(const std::string& inputPath, const std::string& outputPath,
                     const std::string& encoderName, bool allowHardwareDecoders) {
    std::cout << "[Transcoder::run] this=" << this << " input=" << inputPath << std::endl;

    demuxer_.close();
    muxer_.close();
    videoDecoder_.close();
    videoEncoder_.close();
    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
    videoOutStreamIndex_ = -1;
    audioOutStreamIndex_ = -1;

    if (!demuxer_.open(inputPath)) {
        return false;
    }

    if (!muxer_.open(outputPath)) {
        return false;
    }

    if (!initVideo(encoderName, allowHardwareDecoders)) {
        return false;
    }

    if (!initAudio()) {
        return false;
    }

    if (!muxer_.writeHeader()) {
        return false;
    }

    bool success = process();

    if (success) {
        muxer_.writeTrailer();
    }

    return success;
}

bool Transcoder::isHevc(const std::string& inputPath) {
    Demuxer dm;
    if (!dm.open(inputPath)) {
        return false;
    }

    int videoIdx = dm.getVideoStreamIndex();
    if (videoIdx < 0) {
        return false;
    }

    const auto& streams = dm.getStreams();
    bool isHevc = streams[videoIdx].codecParams->codec_id == AV_CODEC_ID_HEVC;

    dm.close();
    return isHevc;
}