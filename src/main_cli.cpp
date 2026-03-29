#include "transcoder.h"
#include <iostream>
#include <string>

int main() {
    std::string inputPath = "d:\\workspace\\MediaForge\\test_data\\泰罗奥特曼01_test.mkv";
    std::string outputPath = "d:\\workspace\\MediaForge\\output\\output_hevc.mkv";

    std::cout << "=== MediaForge CLI Test ===" << std::endl;
    std::cout << "Input:  " << inputPath << std::endl;
    std::cout << "Output: " << outputPath << std::endl;
    std::cout << std::endl;

    Transcoder transcoder;
    transcoder.setProgressCallback([](float progress) {
        std::cout << "\rProgress: " << (int)(progress * 100) << "%" << std::flush;
    });

    bool isHevc = Transcoder::isHevc(inputPath);
    std::cout << "Input is HEVC: " << (isHevc ? "yes" : "no") << std::endl;

    std::string encoder = isHevc ? "hevc" : "libx265";
    std::cout << "Using encoder: " << encoder << std::endl;
    std::cout << std::endl;

    std::cout << "Starting transcoding..." << std::endl;
    bool success = transcoder.run(inputPath, outputPath, encoder, false);

    std::cout << std::endl;
    if (success) {
        std::cout << "Transcoding completed successfully!" << std::endl;
    } else {
        std::cout << "Transcoding failed!" << std::endl;
    }

    return success ? 0 : 1;
}