#include "job_system.h"
#include <iostream>
#include <filesystem>
#include <windows.h> // For MultiByteToWideChar

namespace fs = std::filesystem;

// Helper to convert UTF-8 to Wide String (Duplicated from main.cpp, ideally should be in a common header)
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static fs::path Utf8ToPath(const std::string& str) {
    return fs::path(Utf8ToWide(str));
}

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
    
    if (transcoder.run(job->inputPath, job->outputPath, job->encoder, true)) {
        job->status = JobStatus::Completed;
        job->statusMessage = "Completed";
        job->progress = 1.0f;
    } else {
        // Hardware decoding failed, retry with software decoder
        std::cout << "Hardware decoding failed for " << job->inputPath << ", retrying with software decoder..." << std::endl;
        job->statusMessage = "Retrying (Software)...";
        job->progress = 0.0f;
        
        bool success = false;
        {
            Transcoder softwareTranscoder;
            softwareTranscoder.setProgressCallback([job](float progress) {
                job->progress = progress;
            });
            
            softwareTranscoder.setPauseCallback([this]() {
                return paused.load();
            });
            
            success = softwareTranscoder.run(job->inputPath, job->outputPath, job->encoder, false);
        } // softwareTranscoder destroyed here, file handle closed

        if (success) {
            job->status = JobStatus::Completed;
            job->statusMessage = "Completed (Software)";
            job->progress = 1.0f;
        } else {
            job->status = JobStatus::Failed;
            job->statusMessage = "Failed";
            // Cleanup output file
            try {
                fs::path outPath = Utf8ToPath(job->outputPath);
                if (fs::exists(outPath)) {
                    fs::remove(outPath);
                    std::cout << "Cleaned up failed output file: " << job->outputPath << std::endl;
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Warning: Could not delete failed output file: " << e.what() << std::endl;
                // Don't fail the job just because we couldn't delete the output file
            }
        }
    }
}
