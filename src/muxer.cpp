#include "muxer.h"
#include <iostream>
#include <cstring>
#include <windows.h>

#define NOMINMAX

extern "C" {
#include <libavutil/opt.h>
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static std::string GetAbsolutePath(const std::string& path) {
    if (path.empty()) return path;

    if (path[0] == '/' || path[0] == '\\') {
        return path;
    }

    if (path.length() >= 2 && path[1] == ':') {
        return path;
    }

    char buffer[MAX_PATH];
    if (GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr) > 0) {
        return std::string(buffer);
    }
    return path;
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

Muxer::Muxer() {}

Muxer::~Muxer() {
    close();
}

bool Muxer::open(const std::string& outputPath) {
    std::cout << "[Muxer::open] this=" << this << " path=" << outputPath << std::endl;
    close();

    std::string absPath = GetAbsolutePath(outputPath);

    {
        std::wstring widePath = Utf8ToWide(absPath);
        FILE* f = _wfopen(widePath.c_str(), L"wb");
        if (f) {
            fclose(f);
        } else {
            std::cerr << "[Muxer] Could not create output file: " << absPath << std::endl;
            return false;
        }
    }

    std::string pathForFFmpeg = GetShortPath(absPath);
    std::cout << "[Muxer] Opening output: " << pathForFFmpeg << std::endl;

    const AVOutputFormat* oformat = nullptr;
    if (absPath.find(".mkv") != std::string::npos || absPath.find(".webm") != std::string::npos) {
        oformat = av_guess_format("matroska", nullptr, nullptr);
    } else {
        oformat = av_guess_format("mp4", nullptr, nullptr);
    }

    int ret = avformat_alloc_output_context2(&fmtCtx_, oformat, nullptr, pathForFFmpeg.c_str());
    if (!fmtCtx_) {
        std::cerr << "[Muxer] Could not create output context for " << absPath << std::endl;
        return false;
    }

    std::cout << "[Muxer] Format: " << (fmtCtx_->oformat ? fmtCtx_->oformat->name : "unknown") << std::endl;

    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx_->pb, pathForFFmpeg.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "[Muxer] Could not open output file: " << errbuf << std::endl;
            return false;
        }
    }

    return true;
}

void Muxer::close() {
    std::cout << "[Muxer::close] this=" << this << std::endl;
    if (fmtCtx_) {
        if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmtCtx_->pb);
        }
        avformat_free_context(fmtCtx_);
        fmtCtx_ = nullptr;
    }
    headerWritten_ = false;
    lastDts_.clear();
    lastPts_.clear();
    codecTimeBases_.clear();
}

int Muxer::addStream(AVCodecParameters* codecParams) {
    if (!fmtCtx_) return -1;

    AVStream* stream = avformat_new_stream(fmtCtx_, nullptr);
    if (!stream) return -1;

    avcodec_parameters_copy(stream->codecpar, codecParams);
    lastDts_.push_back(AV_NOPTS_VALUE);
    lastPts_.push_back(AV_NOPTS_VALUE);
    codecTimeBases_.push_back(stream->time_base);

    std::cout << "[Muxer] addStream (params): index=" << stream->index
              << ", codec_type=" << codecParams->codec_type
              << ", codec_id=" << codecParams->codec_id
              << ", time_base=" << stream->time_base.num << "/" << stream->time_base.den << std::endl;

    return stream->index;
}

int Muxer::addStream(AVCodecContext* codecCtx) {
    if (!fmtCtx_) return -1;

    AVStream* stream = avformat_new_stream(fmtCtx_, nullptr);
    if (!stream) return -1;

    avcodec_parameters_from_context(stream->codecpar, codecCtx);
    stream->time_base = codecCtx->time_base;
    lastDts_.push_back(AV_NOPTS_VALUE);
    lastPts_.push_back(AV_NOPTS_VALUE);
    codecTimeBases_.push_back(codecCtx->time_base);

    std::cout << "[Muxer] addStream (ctx): index=" << stream->index
              << ", codec_type=" << codecCtx->codec_type
              << ", codec_id=" << codecCtx->codec_id
              << ", time_base=" << stream->time_base.num << "/" << stream->time_base.den
              << " (codec time_base=" << codecCtx->time_base.num << "/" << codecCtx->time_base.den << ")" << std::endl;

    return stream->index;
}

void Muxer::setStreamTimeBase(int streamIndex, AVRational timeBase) {
    if (fmtCtx_ && streamIndex >= 0 && streamIndex < (int)fmtCtx_->nb_streams) {
        fmtCtx_->streams[streamIndex]->time_base = timeBase;
    }
}

bool Muxer::writeHeader() {
    std::cout << "[Muxer::writeHeader] this=" << this << " fmtCtx=" << fmtCtx_ << " headerWritten=" << headerWritten_ << std::endl;
    if (!fmtCtx_ || headerWritten_) return false;

    std::cout << "[Muxer] writeHeader: " << fmtCtx_->nb_streams << " streams" << std::endl;
    for (int i = 0; i < fmtCtx_->nb_streams; i++) {
        AVStream* s = fmtCtx_->streams[i];
        std::cout << "  Stream " << i << ": codec_type=" << s->codecpar->codec_type
                  << " (video=0,audio=1), codec_id=" << s->codecpar->codec_id
                  << ", time_base=" << s->time_base.num << "/" << s->time_base.den << std::endl;
    }

    AVDictionary* opts = nullptr;
    if (fmtCtx_->oformat && fmtCtx_->oformat->name &&
        (strcmp(fmtCtx_->oformat->name, "mp4") == 0 || strcmp(fmtCtx_->oformat->name, "m4a") == 0)) {
        av_dict_set(&opts, "movflags", "+faststart", 0);
        std::cout << "[Muxer] Setting movflags +faststart for MP4" << std::endl;
    }

    int ret = avformat_write_header(fmtCtx_, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "[Muxer] Error writing header: " << errbuf << " (code " << ret << ")" << std::endl;
        return false;
    }

    headerWritten_ = true;
    std::cout << "[Muxer] Header written successfully" << std::endl;
    return true;
}

bool Muxer::writePacket(AVPacket* packet) {
    if (!fmtCtx_ || !headerWritten_) return false;

    int idx = packet->stream_index;
    AVStream* stream = fmtCtx_->streams[idx];

    AVRational codecTimeBase = codecTimeBases_[idx];
    av_packet_rescale_ts(packet, codecTimeBase, stream->time_base);

    if (idx < (int)lastDts_.size()) {
        if (packet->dts != AV_NOPTS_VALUE) {
            if (lastDts_[idx] != AV_NOPTS_VALUE && packet->dts <= lastDts_[idx]) {
                packet->dts = lastDts_[idx] + 1;
            }
            if (packet->pts != AV_NOPTS_VALUE && packet->pts < packet->dts) {
                packet->pts = packet->dts;
            }
            lastDts_[idx] = packet->dts;
        }
        if (packet->pts != AV_NOPTS_VALUE) {
            if (lastPts_[idx] != AV_NOPTS_VALUE && packet->pts <= lastPts_[idx]) {
                packet->pts = lastPts_[idx] + 1;
            }
            lastPts_[idx] = packet->pts;
        }
    }

    int ret = av_interleaved_write_frame(fmtCtx_, packet);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "[Muxer] Write frame error: " << errbuf << std::endl;
        return false;
    }
    return true;
}

bool Muxer::writeTrailer() {
    if (!fmtCtx_ || !headerWritten_) return false;

    int ret = av_write_trailer(fmtCtx_);
    if (ret < 0) {
        std::cerr << "[Muxer] Error writing trailer" << std::endl;
        return false;
    }

    std::cout << "[Muxer] Trailer written" << std::endl;
    return true;
}