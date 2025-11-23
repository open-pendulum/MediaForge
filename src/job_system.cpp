#include "job_system.h"
#include <iostream>

JobManager::JobManager(int maxConcurrent) : maxConcurrentJobs(maxConcurrent) {
    start();
}

JobManager::~JobManager() {
    stop();
}

void JobManager::start() {
    if (running) return;
    running = true;
    
    for (int i = 0; i < maxConcurrentJobs; ++i) {
        workers.emplace_back(&JobManager::workerLoop, this);
    }
}

void JobManager::stop() {
    if (!running) return;
    running = false;
    cv.notify_all();
    
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers.clear();
}

void JobManager::addJob(const std::string& inputPath, const std::string& outputPath, const std::string& encoder) {
    auto job = std::make_shared<TranscodeJob>(nextJobId++, inputPath, outputPath, encoder);
    
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        jobs.push_back(job);
        pendingQueue.push(job);
    }
    
    cv.notify_one();
}

void JobManager::setPaused(bool p) {
    paused = p;
    if (!paused) {
        cv.notify_all();
    }
}

void JobManager::workerLoop() {
    while (running) {
        std::shared_ptr<TranscodeJob> job;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [this] { 
                return (!pendingQueue.empty() && !paused) || !running; 
            });
            
            if (!running) break;
            
            // If paused, continue waiting (unless stopped)
            if (paused) continue;
            
            if (!pendingQueue.empty()) {
                job = pendingQueue.front();
                pendingQueue.pop();
            }
        }
        
        if (job) {
            activeJobs++;
            processJob(job);
            activeJobs--;
        }
    }
}

void JobManager::processJob(std::shared_ptr<TranscodeJob> job) {
    // Check if already HEVC
    if (Transcoder::isHevc(job->inputPath)) {
        job->status = JobStatus::Skipped;
        job->statusMessage = "Skipped (Already H.265)";
        job->progress = 1.0f;
        return;
    }

    job->status = JobStatus::Running;
    job->statusMessage = "Transcoding...";
    
    Transcoder transcoder;
    transcoder.setProgressCallback([job](float progress) {
        job->progress = progress;
    });
    
    transcoder.setPauseCallback([this]() {
        return paused.load();
    });
    
    if (transcoder.run(job->inputPath, job->outputPath, job->encoder)) {
        job->status = JobStatus::Completed;
        job->statusMessage = "Completed";
        job->progress = 1.0f;
    } else {
        job->status = JobStatus::Failed;
        job->statusMessage = "Failed";
    }
}
