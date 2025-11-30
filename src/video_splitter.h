#pragma once

#include <string>
#include <vector>
#include <functional>

struct CutPoint {
    double time;
    std::string name;
    
    CutPoint(double t, const std::string& n = "") : time(t), name(n) {}
};

struct Segment {
    double startTime;
    double endTime;
    std::string name;
    bool exportEnabled;
    
    Segment(double start, double end, const std::string& n = "", bool enabled = true)
        : startTime(start), endTime(end), name(n), exportEnabled(enabled) {}
    
    double getDuration() const { return endTime - startTime; }
};

class VideoSplitter {
public:
    VideoSplitter();
    ~VideoSplitter();
    
    // Cut point management
    void addCutPoint(double time);
    void removeCutPoint(int index);
    void clearCutPoints();
    const std::vector<CutPoint>& getCutPoints() const { return cutPoints; }
    
    // Segment management
    std::vector<Segment> getSegments(double videoDuration) const;
    
    // Export
    using ProgressCallback = std::function<void(int current, int total, const std::string& message)>;
    bool exportSegments(const std::string& inputPath, 
                       const std::string& outputDir,
                       const std::vector<Segment>& segments,
                       ProgressCallback callback = nullptr);
    
    bool exportSegmentsMerged(const std::string& inputPath,
                             const std::string& outputPath,
                             const std::vector<Segment>& segments,
                             ProgressCallback callback = nullptr);
    
private:
    std::vector<CutPoint> cutPoints;
    
    bool exportSegment(const std::string& inputPath,
                      const std::string& outputPath,
                      double startTime,
                      double duration);
    
    std::string formatTime(double seconds) const;
    std::string generateSegmentName(const std::string& baseName,
                                    double startTime,
                                    double endTime) const;
};
