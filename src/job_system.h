#pragma once

#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "transcoder.h"

enum class JobStatus {
    Pending,
    Running,
    Completed,
    Failed,
    Skipped
};

struct TranscodeJob {
    int id;
    std::string inputPath;
    std::string outputPath;
    std::string encoder;
    std::atomic<float> progress{0.0f};
    std::atomic<JobStatus> status{JobStatus::Pending};
    std::string statusMessage = "Pending";
    
    TranscodeJob(int id, std::string in, std::string out, std::string enc) 
        : id(id), inputPath(in), outputPath(out), encoder(enc) {}
};

class JobManager {
public:
    JobManager(int maxConcurrent = 3);
    ~JobManager();

    void addJob(const std::string& inputPath, const std::string& outputPath, const std::string& encoder = "auto");
    void start();
    void stop();
    
    void setPaused(bool paused);
    bool isPaused() const { return paused; }

    const std::vector<std::shared_ptr<TranscodeJob>>& getJobs() const { return jobs; }

private:
    void workerLoop();
    void processJob(std::shared_ptr<TranscodeJob> job);

    int maxConcurrentJobs;
    std::vector<std::shared_ptr<TranscodeJob>> jobs;
    std::queue<std::shared_ptr<TranscodeJob>> pendingQueue;
    
    std::vector<std::thread> workers;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{true}; // Default to paused
    std::atomic<int> activeJobs{0};
    int nextJobId = 1;
};
