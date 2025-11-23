# ImGui Static Library
cmake_minimum_required(VERSION 3.15)

set(IMGUI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/imgui-1.92.5)

# ImGui core source files
set(IMGUI_SOURCES
    ${IMGUI_ROOT}/imgui.cpp
    ${IMGUI_ROOT}/imgui_demo.cpp
    ${IMGUI_ROOT}/imgui_draw.cpp
    ${IMGUI_ROOT}/imgui_tables.cpp
    ${IMGUI_ROOT}/imgui_widgets.cpp
    # OpenGL3 backend
    ${IMGUI_ROOT}/backends/imgui_impl_opengl3.cpp
    # GLFW backend
    ${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp
)

# ImGui header files
set(IMGUI_HEADERS
    ${IMGUI_ROOT}/imgui.h
    ${IMGUI_ROOT}/imgui_internal.h
    ${IMGUI_ROOT}/backends/imgui_impl_opengl3.h
    ${IMGUI_ROOT}/backends/imgui_impl_glfw.h
)

# Create static library
add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_HEADERS})

# Find OpenGL
find_package(OpenGL REQUIRED)

# Set include directories
target_include_directories(imgui PUBLIC 
    ${IMGUI_ROOT}
    ${IMGUI_ROOT}/backends
    ${IMGUI_ROOT}/examples/libs/glfw/include
    ${OPENGL_INCLUDE_DIR}
)

# Link bundled GLFW and OpenGL
target_link_libraries(imgui PUBLIC
    ${IMGUI_ROOT}/examples/libs/glfw/lib-vc2010-64/glfw3.lib
    OpenGL::GL
)

# Set compiler flags for better performance
if(MSVC)
    target_compile_options(imgui PRIVATE /W4)
else()
    target_compile_options(imgui PRIVATE -Wall -Wextra)
endif()

# Export imgui root directory for backend access
set(IMGUI_BACKENDS_DIR ${IMGUI_ROOT}/backends PARENT_SCOPE)
set(IMGUI_ROOT_DIR ${IMGUI_ROOT} PARENT_SCOPE)
