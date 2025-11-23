cmake_minimum_required(VERSION 3.15)
project(MediaForge)

set(CMAKE_CXX_STANDARD 17)

# 设置统一的输出目录
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR})

# 为不同配置设置相同的输出目录
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${OUTPUT_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${OUTPUT_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${OUTPUT_DIR})
endforeach()

# 添加第三方库 (FFmpeg, ImGui)
add_subdirectory(third_party)

# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

# 可执行文件
add_executable(MediaForge ${SOURCES})

# 链接库
# ImGui 已经配置了 PUBLIC 依赖 (GLFW, OpenGL)，所以这里只需要链接 imgui
target_link_libraries(MediaForge PRIVATE 
    ffmpeg
    imgui
)

# Windows 平台特定配置
if(WIN32)
    # 链接系统库
    # target_link_libraries(MediaForge PRIVATE dwmapi)
endif()

# 复制 FFmpeg DLL 到输出目录
if(WIN32 AND DEFINED FFMPEG_BIN_DIR)
    file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
    foreach(DLL ${FFMPEG_DLLS})
        add_custom_command(TARGET MediaForge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL}"
            "${OUTPUT_DIR}"
            COMMENT "Copying ${DLL} to ${OUTPUT_DIR}"
        )
    endforeach()
endif()
