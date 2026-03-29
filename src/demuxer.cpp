#include "demuxer.h"
#include <iostream>
#include <windows.h>

#define NOMINMAX

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

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

Demuxer::Demuxer() {}

Demuxer::~Demuxer() {
    close();
}

bool Demuxer::open(const std::string& inputPath) {
    close();

    std::string pathForFFmpeg = GetShortPath(inputPath);
    std::cout << "[Demuxer] Opening input: " << pathForFFmpeg << std::endl;

    if (avformat_open_input(&fmtCtx_, pathForFFmpeg.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[Demuxer] Could not open input file: " << inputPath << std::endl;
        return false;
    }

    if (avformat_find_stream_info(fmtCtx_, nullptr) < 0) {
        std::cerr << "[Demuxer] Could not find stream info" << std::endl;
        return false;
    }

    streams_.reserve(fmtCtx_->nb_streams);
    for (unsigned int i = 0; i < fmtCtx_->nb_streams; i++) {
        AVStream* stream = fmtCtx_->streams[i];
        StreamInfo info;
        info.streamIndex = i;
        info.codecType = stream->codecpar->codec_type;
        info.codecParams = stream->codecpar;
        info.timeBase = stream->time_base;
        info.duration = stream->duration;
        streams_.push_back(info);

        if (info.codecType == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ == -1) {
            videoStreamIndex_ = i;
        } else if (info.codecType == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ == -1) {
            audioStreamIndex_ = i;
        }
    }

    std::cout << "[Demuxer] Found " << streams_.size() << " streams (video: " << videoStreamIndex_
              << ", audio: " << audioStreamIndex_ << ")" << std::endl;

    return true;
}

void Demuxer::close() {
    if (fmtCtx_) {
        avformat_close_input(&fmtCtx_);
        fmtCtx_ = nullptr;
    }
    streams_.clear();
    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
}

bool Demuxer::readPacket(AVPacket* packet) {
    if (!fmtCtx_) return false;
    return av_read_frame(fmtCtx_, packet) >= 0;
}

bool Demuxer::seek(int streamIndex, int64_t timestamp, int flags) {
    if (!fmtCtx_) return false;
    return av_seek_frame(fmtCtx_, streamIndex, timestamp, flags) >= 0;
}