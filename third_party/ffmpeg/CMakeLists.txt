# FFmpeg Interface Library
cmake_minimum_required(VERSION 3.15)

set(FFMPEG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/libffmpeg_7.1)
set(FFMPEG_INCLUDE_DIR ${FFMPEG_ROOT}/include)
set(FFMPEG_LIB_DIR ${FFMPEG_ROOT}/lib/x64)
set(FFMPEG_BIN_DIR ${FFMPEG_ROOT}/bin/x64)

# Create interface library for each FFmpeg component
add_library(ffmpeg_avcodec INTERFACE)
target_include_directories(ffmpeg_avcodec INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(ffmpeg_avcodec INTERFACE ${FFMPEG_LIB_DIR}/avcodec.lib)

add_library(ffmpeg_avformat INTERFACE)
target_include_directories(ffmpeg_avformat INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(ffmpeg_avformat INTERFACE ${FFMPEG_LIB_DIR}/avformat.lib)

add_library(ffmpeg_avutil INTERFACE)
target_include_directories(ffmpeg_avutil INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(ffmpeg_avutil INTERFACE ${FFMPEG_LIB_DIR}/avutil.lib)

add_library(ffmpeg_swscale INTERFACE)
target_include_directories(ffmpeg_swscale INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(ffmpeg_swscale INTERFACE ${FFMPEG_LIB_DIR}/swscale.lib)

add_library(ffmpeg_swresample INTERFACE)
target_include_directories(ffmpeg_swresample INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(ffmpeg_swresample INTERFACE ${FFMPEG_LIB_DIR}/swresample.lib)

add_library(ffmpeg_avfilter INTERFACE)
target_include_directories(ffmpeg_avfilter INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(ffmpeg_avfilter INTERFACE ${FFMPEG_LIB_DIR}/avfilter.lib)

add_library(ffmpeg_avdevice INTERFACE)
target_include_directories(ffmpeg_avdevice INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(ffmpeg_avdevice INTERFACE ${FFMPEG_LIB_DIR}/avdevice.lib)

add_library(ffmpeg_postproc INTERFACE)
target_include_directories(ffmpeg_postproc INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(ffmpeg_postproc INTERFACE ${FFMPEG_LIB_DIR}/postproc.lib)

# Create a combined FFmpeg interface library for convenience
add_library(ffmpeg INTERFACE)
target_link_libraries(ffmpeg INTERFACE
    ffmpeg_avcodec
    ffmpeg_avformat
    ffmpeg_avutil
    ffmpeg_swscale
    ffmpeg_swresample
)

# Export the bin directory for DLL copying
set(FFMPEG_BIN_DIR ${FFMPEG_BIN_DIR} PARENT_SCOPE)
