#include "transcoder.h"
#include <iostream>
#include <stdio.h>
#include <iomanip>

// #include "imgui.h"
// #include "imgui_impl_glfw.h"
// #include "imgui_impl_opengl3.h"
// #include <GLFW/glfw3.h> // Will drag system OpenGL headers

// static void glfw_error_callback(int error, const char* description)
// {
//     fprintf(stderr, "GLFW Error %d: %s\n", error, description);
// }

int main() {
    // 硬编码输入输出路径到 data 目录
    std::string inputPath = "../data/test.mp4";
    std::string outputPath = "../data/test_h265.mp4";

    std::cout << "Transcoding " << inputPath << " to " << outputPath << " ..." << std::endl;

    Transcoder transcoder;
    
    // 设置进度回调
    transcoder.setProgressCallback([](float progress) {
        int percent = static_cast<int>(progress * 100);
        std::cout << "\rProgress: " << percent << "%" << std::flush;
    });

    if (transcoder.run(inputPath, outputPath)) {
        std::cout << "\nTranscoding completed successfully." << std::endl;
        return 0;
    } else {
        std::cerr << "\nTranscoding failed." << std::endl;
        return 1;
    }
}
/*
int main_imgui() {
    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Decide GL+GLSL versions
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "MediaForge", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Our state
    bool show_demo_window = true;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Show a simple window
        {
            ImGui::Begin("MediaForge Control Panel");

            ImGui::Text("Welcome to MediaForge!");
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            
            ImGui::Separator();
            
            static char inputPath[256] = "../data/test.mp4";
            static char outputPath[256] = "../data/test_h265.mp4";
            
            ImGui::InputText("Input Path", inputPath, 256);
            ImGui::InputText("Output Path", outputPath, 256);
            
            if (ImGui::Button("Start Transcoding (Simulated)")) {
                std::cout << "Transcoding " << inputPath << " to " << outputPath << " ..." << std::endl;
                // Transcoder transcoder;
                // if (transcoder.run(inputPath, outputPath)) {
                //     std::cout << "Transcoding completed successfully." << std::endl;
                // } else {
                //     std::cerr << "Transcoding failed." << std::endl;
                // }
            }

            ImGui::End();
        }

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
*/
