#include "job_system.h"
#include "transcoder.h"
#include "video_player.h"
#include "video_splitter.h"
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <shlobj.h>

namespace fs = std::filesystem;

extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Helper to convert Wide String to UTF-8
std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Helper to convert UTF-8 to Wide String
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Helper to create fs::path from UTF-8 string correctly on Windows
fs::path Utf8ToPath(const std::string& str) {
    return fs::path(Utf8ToWide(str));
}

// Helper for File Dialog (Unicode)
std::vector<std::string> OpenFileDialog(GLFWwindow* window) {
    std::vector<std::string> selectedFiles;
    
    OPENFILENAMEW ofn;
    wchar_t szFile[2048] = { 0 };
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"Video Files\0*.mp4;*.mkv;*.avi;*.mov\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        wchar_t* p = ofn.lpstrFile;
        std::wstring dir = p;
        p += dir.length() + 1;
        
        if (*p == 0) {
            // Single file selected
            selectedFiles.push_back(WideToUtf8(dir));
        } else {
            // Multiple files selected
            std::string dirUtf8 = WideToUtf8(dir);
            while (*p) {
                std::wstring filename = p;
                selectedFiles.push_back(dirUtf8 + "\\" + WideToUtf8(filename));
                p += filename.length() + 1;
            }
        }
    }
    return selectedFiles;
}

// Helper for Folder Dialog (Unicode)
std::string OpenFolderDialog(GLFWwindow* window) {
    wchar_t path[MAX_PATH];
    BROWSEINFOW bi = { 0 };
    bi.lpszTitle = L"Select Output Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.hwndOwner = glfwGetWin32Window(window);

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);

    if (pidl != 0) {
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            return WideToUtf8(path);
        }
        CoTaskMemFree(pidl);
    }
    return "";
}

void loadConfig(std::string& outputFolder) {
    std::ifstream configFile("config.ini");
    if (configFile.is_open()) {
        std::getline(configFile, outputFolder);
        configFile.close();
    }
}

void saveConfig(const std::string& outputFolder) {
    std::ofstream configFile("config.ini");
    if (configFile.is_open()) {
        configFile << outputFolder;
        configFile.close();
    }
}

// Generate unique filename by adding numeric suffix if file exists
std::string generateUniqueFilename(const fs::path& basePath) {
    if (!fs::exists(basePath)) {
        return basePath.string(); // Note: .string() returns UTF-8 if path was constructed from wstring? No, on Windows it returns ANSI usually.
        // Actually, we should return the UTF-8 string we started with if possible, or convert back.
        // But here basePath is constructed from Utf8ToPath, so it holds wstring internally on Windows.
        // .string() on Windows converts wstring to ANSI (system code page), which might lose characters!
        // We should use .u8string() (C++20) or convert manually.
        // Since we are C++17 likely, let's use WideToUtf8(basePath.wstring())
        return WideToUtf8(basePath.wstring());
    }
    
    fs::path dir = basePath.parent_path();
    // stem() and extension() return path objects, so we need to convert them to wstring then to UTF-8
    std::string stem = WideToUtf8(basePath.stem().wstring());
    std::string ext = WideToUtf8(basePath.extension().wstring());
    
    int counter = 1;
    while (true) {
        // Reconstruct path using UTF-8 components converted to path
        fs::path newPath = dir / Utf8ToPath(stem + "_" + std::to_string(counter) + ext);
        if (!fs::exists(newPath)) {
            return WideToUtf8(newPath.wstring());
        }
        counter++;
        if (counter > 1000) {
            // Safety check to prevent infinite loop
            std::cerr << "Too many duplicate files, giving up" << std::endl;
            return WideToUtf8(basePath.wstring());
        }
    }
}

enum class AppState {
    Home,
    Transcode,
    Split,
    Merge
};

AppState g_appState = AppState::Home;

void SetupFullScreenWindow() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
}

void ShowMainMenu(GLFWwindow* window) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(window, true);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Transcode")) g_appState = AppState::Transcode;
            if (ImGui::MenuItem("Split")) g_appState = AppState::Split;
            if (ImGui::MenuItem("Merge")) g_appState = AppState::Merge;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void ShowHomeUI() {
    SetupFullScreenWindow();
    ImGui::Begin("Welcome to MediaForge", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    
    ImGui::Text("Select a tool to get started:");
    
    if (ImGui::Button("Video Transcoder", ImVec2(200, 50))) {
        g_appState = AppState::Transcode;
    }
    ImGui::SameLine();
    if (ImGui::Button("Video Splitter", ImVec2(200, 50))) {
        g_appState = AppState::Split;
    }
    ImGui::SameLine();
    if (ImGui::Button("Video Merger", ImVec2(200, 50))) {
        g_appState = AppState::Merge;
    }
    
    ImGui::End();
}

void ShowTranscodeUI(JobManager& jobManager, std::string& outputFolder, int& currentEncoder, const char* encoders[], const char* encoderIds[], GLFWwindow* window, bool* p_open) {
    SetupFullScreenWindow();
    if (!ImGui::Begin("Video Transcoder", p_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Output Folder: %s", outputFolder.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Select Output Folder")) {
        std::string folder = OpenFolderDialog(window);
        if (!folder.empty()) {
            outputFolder = folder;
            saveConfig(outputFolder);
        }
    }
    
    ImGui::Separator();
    
    ImGui::Text("Encoder:");
    ImGui::SameLine();
    ImGui::Combo("##encoder", &currentEncoder, encoders, 5);

    ImGui::Separator();

    if (ImGui::Button("Add Files")) {
        std::vector<std::string> files = OpenFileDialog(window);
        for (const auto& file : files) {
            if (Transcoder::isHevc(file)) {
                std::cout << "Skipping " << file << " as it is already HEVC." << std::endl;
                continue;
            }
            fs::path p = Utf8ToPath(file);
            std::string filename = WideToUtf8(p.filename().wstring());
            std::string stem = WideToUtf8(p.stem().wstring());
            std::string extension = WideToUtf8(p.extension().wstring());
            
            // Construct output path: outputFolder / filename_h265.extension
            fs::path outPath = Utf8ToPath(outputFolder) / Utf8ToPath(stem + "_h265" + extension);
            std::string uniqueOutPath = generateUniqueFilename(outPath);
            
            jobManager.addJob(file, uniqueOutPath, encoderIds[currentEncoder]);
        }
    }
    
    ImGui::SameLine();
    
    // Start/Stop Buttons
    bool isPaused = jobManager.isPaused();
    if (isPaused) {
        if (ImGui::Button("Start Processing")) {
            jobManager.setPaused(false);
        }
    } else {
        if (ImGui::Button("Stop Processing")) {
            jobManager.setPaused(true);
        }
    }
    
    ImGui::SameLine();
    ImGui::Text("Status: %s", isPaused ? "Paused" : "Running");

    ImGui::Separator();
    ImGui::Text("Jobs:");

    const auto& jobs = jobManager.getJobs();
    for (const auto& job : jobs) {
        ImGui::PushID(job->id);
        
        fs::path p = Utf8ToPath(job->inputPath);
        ImGui::Text("%s", WideToUtf8(p.filename().wstring()).c_str());
        
        float progress = job->progress;
        char buf[32];
        sprintf(buf, "%.0f%%", progress * 100.0f);
        
        if (job->status == JobStatus::Failed) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        }
        
        ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f), buf);
        
        if (job->status == JobStatus::Failed) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        ImGui::Text("%s", job->statusMessage.c_str());
        
        ImGui::PopID();
    }

    ImGui::End();
}

void ShowSplitUI(GLFWwindow* window, bool* p_open) {
    static VideoPlayer player;
    static VideoSplitter splitter;
    static std::string currentVideoPath;
    static std::string outputDirectory;
    static GLuint videoTexture = 0;
    static std::vector<Segment> segments;
    static std::string exportMessage;
    static bool isExporting = false;
    static bool showExportDialog = false;
    static int exportMode = 0; // 0 = separate, 1 = merge
    static char mergedFilename[256] = "merged_output";
    
    SetupFullScreenWindow();
    if (!ImGui::Begin("Video Splitter", p_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    
    // Top section: File selection
    ImGui::Text("Video File:");
    ImGui::SameLine();
    if (ImGui::Button("Open Video")) {
        std::vector<std::string> files = OpenFileDialog(window);
        if (!files.empty()) {
            currentVideoPath = files[0];
            // Set default output directory to source file's directory
            fs::path inputPath = Utf8ToPath(currentVideoPath);
            outputDirectory = WideToUtf8(inputPath.parent_path().wstring());
            
            // Set default merged filename: basename_merged
            std::string baseName = WideToUtf8(inputPath.stem().wstring());
            std::string defaultMergedName = baseName + "_merged";
            strncpy_s(mergedFilename, defaultMergedName.c_str(), sizeof(mergedFilename) - 1);
            
            if (player.open(currentVideoPath)) {
                player.decodeNextFrame();
                splitter.clearCutPoints();
                segments.clear();
                
                // Create OpenGL texture for video
                if (videoTexture == 0) {
                    glGenTextures(1, &videoTexture);
                }
                glBindTexture(GL_TEXTURE_2D, videoTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
        }
    }
    
    if (!currentVideoPath.empty()) {
        ImGui::SameLine();
        ImGui::Text("%s", currentVideoPath.c_str());
    }
    
    // Output directory selection
    ImGui::Text("Output Directory:");
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::string folder = OpenFolderDialog(window);
        if (!folder.empty()) {
            outputDirectory = folder;
        }
    }
    if (!outputDirectory.empty()) {
        ImGui::SameLine();
        ImGui::Text("%s", outputDirectory.c_str());
    }
    
    ImGui::Separator();
    
    if (player.getWidth() > 0) {
        // Split into left (video) and right (controls) panels
        ImGui::BeginChild("LeftPanel", ImVec2(ImGui::GetContentRegionAvail().x * 0.7f, 0), true);
        
        // Video preview area
        uint8_t* rgbData = nullptr;
        int width = 0, height = 0;
        if (player.getRGBFrame(&rgbData, &width, &height)) {
            glBindTexture(GL_TEXTURE_2D, videoTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgbData);
            
            // Calculate display size while maintaining aspect ratio
            float availWidth = ImGui::GetContentRegionAvail().x - 10;
            float availHeight = ImGui::GetContentRegionAvail().y - 120;
            float aspectRatio = (float)width / height;
            
            float displayWidth = availWidth;
            float displayHeight = displayWidth / aspectRatio;
            
            if (displayHeight > availHeight) {
                displayHeight = availHeight;
                displayWidth = displayHeight * aspectRatio;
            }
            
            // Center the image
            float offsetX = (availWidth - displayWidth) / 2;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            
            ImGui::Image((ImTextureID)(intptr_t)videoTexture, ImVec2(displayWidth, displayHeight));
        }
        
        // Playback controls
        ImGui::Separator();
        
        // Progress bar
        float currentTime = (float)player.getCurrentTime();
        float duration = (float)player.getDuration();
        float progress = duration > 0 ? currentTime / duration : 0;
        
        ImGui::PushItemWidth(-1);
        if (ImGui::SliderFloat("##progress", &progress, 0.0f, 1.0f, "")) {
            player.seekTo(progress * duration);
        }
        ImGui::PopItemWidth();
        
        // Time display
        int currentMin = (int)(currentTime / 60);
        int currentSec = (int)currentTime % 60;
        int totalMin = (int)(duration / 60);
        int totalSec = (int)duration % 60;
        ImGui::Text("%02d:%02d / %02d:%02d", currentMin, currentSec, totalMin, totalSec);
        
        // Control buttons
        ImGui::SameLine();
        if (player.isPlaying() && !player.isPaused()) {
            if (ImGui::Button("Pause")) {
                player.pause();
            }
        } else {
            if (ImGui::Button("Play")) {
                player.play();
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            player.stop();
            player.seekTo(0);
        }
        
        ImGui::EndChild();
        
        // Right panel: Cut point management
        ImGui::SameLine();
        ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);
        
        ImGui::Text("Cut Point Management");
        ImGui::Separator();
        
        if (ImGui::Button("Add Cut Point at Current Time", ImVec2(-1, 0))) {
            double time = player.getCurrentTime();
            splitter.addCutPoint(time);
            segments = splitter.getSegments(player.getDuration());
        }
        
        ImGui::Separator();
        ImGui::Text("Segments:");
        
        // Regenerate segments from cut points
        if (segments.empty() || segments.size() != splitter.getCutPoints().size() + 1) {
            segments = splitter.getSegments(player.getDuration());
        }
        
        // Display segments
        ImGui::BeginChild("SegmentsList", ImVec2(0, -80), true);
        for (size_t i = 0; i < segments.size(); i++) {
            ImGui::PushID((int)i);
            
            Segment& seg = segments[i];
            
            ImGui::Checkbox("##export", &seg.exportEnabled);
            ImGui::SameLine();
            
            char nameBuf[256];
            strncpy_s(nameBuf, seg.name.c_str(), sizeof(nameBuf) - 1);
            ImGui::SetNextItemWidth(200);
            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
                seg.name = nameBuf;
            }
            
            ImGui::SameLine();
            int startMin = (int)(seg.startTime / 60);
            int startSec = (int)seg.startTime % 60;
            int endMin = (int)(seg.endTime / 60);
            int endSec = (int)seg.endTime % 60;
            ImGui::Text("%02d:%02d - %02d:%02d", startMin, startSec, endMin, endSec);
            
            // Delete button for cut points (not for first/last segment edges)
            if (i < segments.size() - 1) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete Cut")) {
                    splitter.removeCutPoint((int)i);
                    segments = splitter.getSegments(player.getDuration());
                }
            }
            
            ImGui::PopID();
        }
        ImGui::EndChild();
        
        // Export section
        ImGui::Separator();
        
        if (!isExporting) {
            if (ImGui::Button("Start Export", ImVec2(-1, 0))) {
                showExportDialog = true;
            }
        } else {
            ImGui::Text("%s", exportMessage.c_str());
        }
        
        // Export mode dialog
        if (showExportDialog) {
            ImGui::OpenPopup("Export Mode");
            showExportDialog = false;
        }
        
        if (ImGui::BeginPopupModal("Export Mode", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Choose export mode:");
            ImGui::Separator();
            
            ImGui::RadioButton("Export as Separate Files", &exportMode, 0);
            ImGui::RadioButton("Merge into One File", &exportMode, 1);
            
            if (exportMode == 1) {
                ImGui::Separator();
                ImGui::Text("Output filename:");
                ImGui::InputText("##filename", mergedFilename, sizeof(mergedFilename));
            }
            
            ImGui::Separator();
            
            if (ImGui::Button("Confirm", ImVec2(120, 0))) {
                if (outputDirectory.empty()) {
                    exportMessage = "Please select an output directory!";
                } else {
                    isExporting = true;
                    exportMessage = "Exporting...";
                    
                    bool success = false;
                    if (exportMode == 0) {
                        // Separate export
                        success = splitter.exportSegments(currentVideoPath, outputDirectory, segments,
                            [](int current, int total, const std::string& msg) {
                                std::cout << "[" << current << "/" << total << "] " << msg << std::endl;
                            });
                    } else {
                        // Merge export
                        fs::path inputPath = Utf8ToPath(currentVideoPath);
                        std::string extension = WideToUtf8(inputPath.extension().wstring());
                        fs::path outputPath = Utf8ToPath(outputDirectory) / Utf8ToPath(std::string(mergedFilename) + extension);
                        std::string outputPathStr = WideToUtf8(outputPath.wstring());
                        
                        success = splitter.exportSegmentsMerged(currentVideoPath, outputPathStr, segments,
                            [](int current, int total, const std::string& msg) {
                                std::cout << "[" << current << "/" << total << "] " << msg << std::endl;
                            });
                    }
                    
                    if (success) {
                        exportMessage = "Export completed successfully!";
                    } else {
                        exportMessage = "Export failed!";
                    }
                    isExporting = false;
                }
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
        
        ImGui::EndChild();
        
        // Auto-play: decode next frame if playing
        if (player.isPlaying() && !player.isPaused()) {
            static auto lastFrameTime = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            double frameDuration = 1.0 / player.getFPS();
            
            if (std::chrono::duration<double>(now - lastFrameTime).count() >= frameDuration) {
                if (!player.decodeNextFrame()) {
                    player.stop();
                }
                lastFrameTime = now;
            }
        }
    } else {
        ImGui::Text("Please open a video file to begin.");
    }
    
    ImGui::End();
}

int main() {
    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "MediaForge", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    
    // Load Fonts - Try Microsoft YaHei for Chinese support, fallback to Segoe UI
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 20.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
    if (font == NULL) {
        font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f);
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    bool show_demo_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Job Manager
    JobManager jobManager(3); // Limit to 3 concurrent jobs
    std::string outputFolder = "../data";
    loadConfig(outputFolder);
    
    // Encoder Selection
    const char* encoders[] = { "Auto", "NVIDIA (hevc_nvenc)", "Intel (hevc_qsv)", "AMD (hevc_amf)", "CPU (libx265)" };
    const char* encoderIds[] = { "auto", "hevc_nvenc", "hevc_qsv", "hevc_amf", "libx265" };
    static int currentEncoder = 0;

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ShowMainMenu(window);

        switch (g_appState) {
            case AppState::Home:
                ShowHomeUI();
                break;
            case AppState::Transcode:
                {
                    bool open = true;
                    ShowTranscodeUI(jobManager, outputFolder, currentEncoder, encoders, encoderIds, window, &open);
                    if (!open) g_appState = AppState::Home;
                }
                break;
            case AppState::Split:
                {
                    bool open = true;
                    ShowSplitUI(window, &open);
                    if (!open) g_appState = AppState::Home;
                }
                break;
            case AppState::Merge:
                {
                    bool open = true;
                    SetupFullScreenWindow();
                    if (ImGui::Begin("Video Merger", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
                        ImGui::Text("Coming Soon...");
                    }
                    ImGui::End();
                    if (!open) g_appState = AppState::Home;
                }
                break;
        }

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
