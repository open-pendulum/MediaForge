#include "job_system.h"
#include <iostream>
#include <stdio.h>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>

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

        {
            ImGui::Begin("MediaForge Control Panel");

            ImGui::Text("Output Folder: %s", outputFolder.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Select Output Folder")) {
                std::string folder = OpenFolderDialog(window);
                if (!folder.empty()) {
                    outputFolder = folder;
                }
            }
            
            ImGui::Separator();
            
            ImGui::Text("Encoder:");
            ImGui::SameLine();
            ImGui::Combo("##encoder", &currentEncoder, encoders, IM_ARRAYSIZE(encoders));

            ImGui::Separator();

            if (ImGui::Button("Add Files")) {
                std::vector<std::string> files = OpenFileDialog(window);
                for (const auto& file : files) {
                    fs::path p(file);
                    std::string filename = p.filename().string();
                    std::string stem = p.stem().string();
                    std::string extension = p.extension().string();
                    
                    // Construct output path: outputFolder / filename_h265.extension
                    fs::path outPath = fs::path(outputFolder) / (stem + "_h265" + extension);
                    
                    jobManager.addJob(file, outPath.string(), encoderIds[currentEncoder]);
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
                
                fs::path p(job->inputPath);
                ImGui::Text("%s", p.filename().string().c_str());
                
                float progress = job->progress;
                char buf[32];
                sprintf(buf, "%.0f%%", progress * 100.0f);
                ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f), buf);
                ImGui::SameLine();
                ImGui::Text("%s", job->statusMessage.c_str());
                
                ImGui::PopID();
            }

            ImGui::End();
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
