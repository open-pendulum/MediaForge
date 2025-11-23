add_subdirectory(ffmpeg)
add_subdirectory(imgui)

# Propagate variables to parent scope
set(FFMPEG_BIN_DIR ${FFMPEG_BIN_DIR} PARENT_SCOPE)