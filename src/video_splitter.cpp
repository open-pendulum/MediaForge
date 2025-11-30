#include "video_splitter.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#define NOMINMAX
#include <windows.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace fs = std::filesystem;

// Helper to convert UTF-8 to Wide String
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Helper to convert Wide String to UTF-8
static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Helper to create fs::path from UTF-8 string
static fs::path Utf8ToPath(const std::string& str) {
    return fs::path(Utf8ToWide(str));
}

// Helper to get Short Path for FFmpeg
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

VideoSplitter::VideoSplitter() {
}

VideoSplitter::~VideoSplitter() {
}

void VideoSplitter::addCutPoint(double time) {
    CutPoint cp(time);
    
    // Insert in sorted order
    auto it = std::lower_bound(cutPoints.begin(), cutPoints.end(), cp,
        [](const CutPoint& a, const CutPoint& b) { return a.time < b.time; });
    
    // Avoid duplicates (within 0.1 second tolerance)
    if (it != cutPoints.end() && std::abs(it->time - time) < 0.1) {
        return;
    }
    
    cutPoints.insert(it, cp);
}

void VideoSplitter::removeCutPoint(int index) {
    if (index >= 0 && index < (int)cutPoints.size()) {
        cutPoints.erase(cutPoints.begin() + index);
    }
}

void VideoSplitter::clearCutPoints() {
    cutPoints.clear();
}

std::vector<Segment> VideoSplitter::getSegments(double videoDuration) const {
    std::vector<Segment> segments;
    
    if (cutPoints.empty()) {
        // No cut points - entire video is one segment
        segments.push_back(Segment(0.0, videoDuration, "Full Video"));
        return segments;
    }
    
    // First segment: 0 to first cut point
    segments.push_back(Segment(0.0, cutPoints[0].time, 
        "Segment 1: " + formatTime(0.0) + " - " + formatTime(cutPoints[0].time)));
    
    // Middle segments: between cut points
    for (size_t i = 0; i < cutPoints.size() - 1; i++) {
        double start = cutPoints[i].time;
        double end = cutPoints[i + 1].time;
        std::string name = "Segment " + std::to_string(i + 2) + ": " + 
                          formatTime(start) + " - " + formatTime(end);
        segments.push_back(Segment(start, end, name));
    }
    
    // Last segment: last cut point to end
    double lastCutTime = cutPoints.back().time;
    if (lastCutTime < videoDuration) {
        std::string name = "Segment " + std::to_string(cutPoints.size() + 1) + ": " +
                          formatTime(lastCutTime) + " - " + formatTime(videoDuration);
        segments.push_back(Segment(lastCutTime, videoDuration, name));
    }
    
    return segments;
}

std::string VideoSplitter::formatTime(double seconds) const {
    int hours = (int)(seconds / 3600);
    int minutes = (int)((seconds - hours * 3600) / 60);
    int secs = (int)(seconds - hours * 3600 - minutes * 60);
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << secs;
    return oss.str();
}

std::string VideoSplitter::generateSegmentName(const std::string& baseName,
                                               double startTime,
                                               double endTime) const {
    std::string startStr = formatTime(startTime);
    std::string endStr = formatTime(endTime);
    
    // Replace : with - for filename compatibility
    std::replace(startStr.begin(), startStr.end(), ':', '-');
    std::replace(endStr.begin(), endStr.end(), ':', '-');
    
    return baseName + "_" + startStr + "_to_" + endStr;
}

bool VideoSplitter::exportSegment(const std::string& inputPath,
                                  const std::string& outputPath,
                                  double startTime,
                                  double duration) {
    AVFormatContext* inputFmt = nullptr;
    AVFormatContext* outputFmt = nullptr;
    
    std::string inputShortPath = GetShortPath(inputPath);
    
    // Create output file first to get short path
    {
        std::wstring wideOutPath = Utf8ToWide(outputPath);
        FILE* f = _wfopen(wideOutPath.c_str(), L"wb");
        if (f) {
            fclose(f);
        } else {
            std::cerr << "Could not create output file: " << outputPath << std::endl;
            return false;
        }
    }
    
    std::string outputShortPath = GetShortPath(outputPath);
    
    // Open input
    if (avformat_open_input(&inputFmt, inputShortPath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file" << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(inputFmt, nullptr) < 0) {
        std::cerr << "Could not find stream info" << std::endl;
        avformat_close_input(&inputFmt);
        return false;
    }
    
    // Seek to start time
    int64_t startPts = (int64_t)(startTime * AV_TIME_BASE);
    if (av_seek_frame(inputFmt, -1, startPts, AVSEEK_FLAG_BACKWARD) < 0) {
        std::cerr << "Seek failed" << std::endl;
    }
    
    // Create output
    if (avformat_alloc_output_context2(&outputFmt, nullptr, nullptr, outputShortPath.c_str()) < 0) {
        std::cerr << "Could not create output context" << std::endl;
        avformat_close_input(&inputFmt);
        return false;
    }
    
    // Copy streams
    for (unsigned int i = 0; i < inputFmt->nb_streams; i++) {
        AVStream* inStream = inputFmt->streams[i];
        AVStream* outStream = avformat_new_stream(outputFmt, nullptr);
        
        if (!outStream) {
            std::cerr << "Failed to allocate output stream" << std::endl;
            avformat_free_context(outputFmt);
            avformat_close_input(&inputFmt);
            return false;
        }
        
        avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        outStream->codecpar->codec_tag = 0;
        outStream->time_base = inStream->time_base;
    }
    
    // Open output file
    if (!(outputFmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputFmt->pb, outputShortPath.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file" << std::endl;
            avformat_free_context(outputFmt);
            avformat_close_input(&inputFmt);
            return false;
        }
    }
    
    // Write header
    if (avformat_write_header(outputFmt, nullptr) < 0) {
        std::cerr << "Error writing header" << std::endl;
        if (!(outputFmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputFmt->pb);
        avformat_free_context(outputFmt);
        avformat_close_input(&inputFmt);
        return false;
    }
    
    // Copy packets
    AVPacket* pkt = av_packet_alloc();
    int64_t endPts = (int64_t)((startTime + duration) * AV_TIME_BASE);
    
    while (av_read_frame(inputFmt, pkt) >= 0) {
        AVStream* inStream = inputFmt->streams[pkt->stream_index];
        AVStream* outStream = outputFmt->streams[pkt->stream_index];
        
        // Check if we've reached the end time
        int64_t pktTime = av_rescale_q(pkt->pts, inStream->time_base, AV_TIME_BASE_Q);
        if (pktTime > endPts) {
            av_packet_unref(pkt);
            break;
        }
        
        // Rescale timestamps
        pkt->pts = av_rescale_q_rnd(pkt->pts, inStream->time_base, outStream->time_base,
                                    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->dts = av_rescale_q_rnd(pkt->dts, inStream->time_base, outStream->time_base,
                                    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->duration = av_rescale_q(pkt->duration, inStream->time_base, outStream->time_base);
        pkt->pos = -1;
        
        if (av_interleaved_write_frame(outputFmt, pkt) < 0) {
            std::cerr << "Error writing frame" << std::endl;
        }
        
        av_packet_unref(pkt);
    }
    
    av_packet_free(&pkt);
    
    // Write trailer
    av_write_trailer(outputFmt);
    
    // Cleanup
    if (!(outputFmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputFmt->pb);
    avformat_free_context(outputFmt);
    avformat_close_input(&inputFmt);
    
    return true;
}

bool VideoSplitter::exportSegments(const std::string& inputPath,
                                   const std::string& outputDir,
                                   const std::vector<Segment>& segments,
                                   ProgressCallback callback) {
    // Get base name from input path
    fs::path inputPathObj = Utf8ToPath(inputPath);
    std::string baseName = WideToUtf8(inputPathObj.stem().wstring());
    std::string extension = WideToUtf8(inputPathObj.extension().wstring());
    
    // Ensure output directory exists
    fs::path outputDirPath = Utf8ToPath(outputDir);
    if (!fs::exists(outputDirPath)) {
        fs::create_directories(outputDirPath);
    }
    
    int current = 0;
    int total = 0;
    for (const auto& seg : segments) {
        if (seg.exportEnabled) total++;
    }
    
    for (const auto& segment : segments) {
        if (!segment.exportEnabled) continue;
        
        current++;
        
        if (callback) {
            callback(current, total, "Exporting " + segment.name + "...");
        }
        
        // Generate output filename
        std::string outputName = generateSegmentName(baseName, segment.startTime, segment.endTime);
        fs::path outputPath = outputDirPath / Utf8ToPath(outputName + extension);
        std::string outputPathStr = WideToUtf8(outputPath.wstring());
        
        std::cout << "Exporting segment: " << outputPathStr << std::endl;
        
        if (!exportSegment(inputPath, outputPathStr, segment.startTime, segment.getDuration())) {
            std::cerr << "Failed to export segment: " << segment.name << std::endl;
            return false;
        }
    }
    
    if (callback) {
        callback(total, total, "Export completed!");
    }
    
    return true;
}

bool VideoSplitter::exportSegmentsMerged(const std::string& inputPath,
                                         const std::string& outputPath,
                                         const std::vector<Segment>& segments,
                                         ProgressCallback callback) {
    if (callback) {
        callback(0, 1, "Preparing merge export...");
    }
    
    // Create temporary directory for intermediate files
    fs::path tempDir = fs::temp_directory_path() / "mediaforge_merge";
    fs::create_directories(tempDir);
    
    // Get file extension
    fs::path inputPathObj = Utf8ToPath(inputPath);
    std::string extension = WideToUtf8(inputPathObj.extension().wstring());
    
    // Export selected segments to temp files
    std::vector<std::string> tempFiles;
    int current = 0;
    int total = 0;
    for (const auto& seg : segments) {
        if (seg.exportEnabled) total++;
    }
    
    for (size_t i = 0; i < segments.size(); i++) {
        const auto& segment = segments[i];
        if (!segment.exportEnabled) continue;
        
        current++;
        if (callback) {
            std::ostringstream msg;
            msg << "Extracting segment " << current << " of " << total << "...";
            callback(current, total + 1, msg.str());
        }
        
        // Create temp file for this segment
        std::string tempFileName = "segment_" + std::to_string(i) + extension;
        fs::path tempFilePath = tempDir / tempFileName;
        std::string tempFileStr = WideToUtf8(tempFilePath.wstring());
        
        if (!exportSegment(inputPath, tempFileStr, segment.startTime, segment.getDuration())) {
            std::cerr << "Failed to export segment for merge" << std::endl;
            for (const auto& tf : tempFiles) {
                fs::remove(Utf8ToPath(tf));
            }
            fs::remove_all(tempDir);
            return false;
        }
        
        tempFiles.push_back(tempFileStr);
    }
    
    if (callback) {
        callback(total, total + 1, "Merging segments...");
    }
    
    // Create concat list file
    fs::path concatListPath = tempDir / "concat_list.txt";
    std::wstring wideConcatPath = Utf8ToWide(WideToUtf8(concatListPath.wstring()));
    FILE* concatFile = _wfopen(wideConcatPath.c_str(), L"w");
    if (!concatFile) {
        std::cerr << "Could not create concat list file" << std::endl;
        for (const auto& tf : tempFiles) {
            fs::remove(Utf8ToPath(tf));
        }
        fs::remove_all(tempDir);
        return false;
    }
    
    for (const auto& tempFile : tempFiles) {
        std::string shortPath = GetShortPath(tempFile);
        std::string escapedPath = shortPath;
        size_t pos = 0;
        while ((pos = escapedPath.find("'", pos)) != std::string::npos) {
            escapedPath.replace(pos, 1, "'\\''");
            pos += 4;
        }
        fprintf(concatFile, "file '%s'\n", escapedPath.c_str());
    }
    fclose(concatFile);
    
    // Use FFmpeg to concat
    AVFormatContext* inputFmt = nullptr;
    AVFormatContext* outputFmt = nullptr;
    
    std::string concatListStr = GetShortPath(WideToUtf8(concatListPath.wstring()));
    
    const AVInputFormat* concatFormat = av_find_input_format("concat");
    if (!concatFormat) {
        std::cerr << "concat demuxer not found" << std::endl;
        for (const auto& tf : tempFiles) {
            fs::remove(Utf8ToPath(tf));
        }
        fs::remove_all(tempDir);
        return false;
    }
    
    AVDictionary* options = nullptr;
    av_dict_set(&options, "safe", "0", 0);
    
    if (avformat_open_input(&inputFmt, concatListStr.c_str(), concatFormat, &options) < 0) {
        std::cerr << "Could not open concat list" << std::endl;
        av_dict_free(&options);
        for (const auto& tf : tempFiles) {
            fs::remove(Utf8ToPath(tf));
        }
        fs::remove_all(tempDir);
        return false;
    }
    av_dict_free(&options);
    
    if (avformat_find_stream_info(inputFmt, nullptr) < 0) {
        std::cerr << "Could not find stream info" << std::endl;
        avformat_close_input(&inputFmt);
        for (const auto& tf : tempFiles) {
            fs::remove(Utf8ToPath(tf));
        }
        fs::remove_all(tempDir);
        return false;
    }
    
    // Create output file
    {
        std::wstring wideOutPath = Utf8ToWide(outputPath);
        FILE* f = _wfopen(wideOutPath.c_str(), L"wb");
        if (f) {
            fclose(f);
        } else {
            std::cerr << "Could not create output file: " << outputPath << std::endl;
            avformat_close_input(&inputFmt);
            for (const auto& tf : tempFiles) {
                fs::remove(Utf8ToPath(tf));
            }
            fs::remove_all(tempDir);
            return false;
        }
    }
    
    std::string outputShortPath = GetShortPath(outputPath);
    
    if (avformat_alloc_output_context2(&outputFmt, nullptr, nullptr, outputShortPath.c_str()) < 0) {
        std::cerr << "Could not create output context" << std::endl;
        avformat_close_input(&inputFmt);
        for (const auto& tf : tempFiles) {
            fs::remove(Utf8ToPath(tf));
        }
        fs::remove_all(tempDir);
        return false;
    }
    
    // Copy streams
    for (unsigned int i = 0; i < inputFmt->nb_streams; i++) {
        AVStream* inStream = inputFmt->streams[i];
        AVStream* outStream = avformat_new_stream(outputFmt, nullptr);
        
        if (!outStream) {
            std::cerr << "Failed to allocate output stream" << std::endl;
            avformat_free_context(outputFmt);
            avformat_close_input(&inputFmt);
            for (const auto& tf : tempFiles) {
                fs::remove(Utf8ToPath(tf));
            }
            fs::remove_all(tempDir);
            return false;
        }
        
        avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        outStream->codecpar->codec_tag = 0;
        outStream->time_base = inStream->time_base;
    }
    
    // Open output file
    if (!(outputFmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputFmt->pb, outputShortPath.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file" << std::endl;
            avformat_free_context(outputFmt);
            avformat_close_input(&inputFmt);
            for (const auto& tf : tempFiles) {
                fs::remove(Utf8ToPath(tf));
            }
            fs::remove_all(tempDir);
            return false;
        }
    }
    
    // Write header
    if (avformat_write_header(outputFmt, nullptr) < 0) {
        std::cerr << "Error writing header" << std::endl;
        if (!(outputFmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputFmt->pb);
        avformat_free_context(outputFmt);
        avformat_close_input(&inputFmt);
        for (const auto& tf : tempFiles) {
            fs::remove(Utf8ToPath(tf));
        }
        fs::remove_all(tempDir);
        return false;
    }
    
    // Copy packets
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(inputFmt, pkt) >= 0) {
        AVStream* inStream = inputFmt->streams[pkt->stream_index];
        AVStream* outStream = outputFmt->streams[pkt->stream_index];
        
        pkt->pts = av_rescale_q_rnd(pkt->pts, inStream->time_base, outStream->time_base,
                                    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->dts = av_rescale_q_rnd(pkt->dts, inStream->time_base, outStream->time_base,
                                    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->duration = av_rescale_q(pkt->duration, inStream->time_base, outStream->time_base);
        pkt->pos = -1;
        
        if (av_interleaved_write_frame(outputFmt, pkt) < 0) {
            std::cerr << "Error writing frame" << std::endl;
        }
        
        av_packet_unref(pkt);
    }
    
    av_packet_free(&pkt);
    av_write_trailer(outputFmt);
    
    // Cleanup
    if (!(outputFmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputFmt->pb);
    avformat_free_context(outputFmt);
    avformat_close_input(&inputFmt);
    
    for (const auto& tf : tempFiles) {
        fs::remove(Utf8ToPath(tf));
    }
    fs::remove_all(tempDir);
    
    if (callback) {
        callback(total + 1, total + 1, "Merge completed!");
    }
    
    return true;
}
