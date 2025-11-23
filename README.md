# Media Processor

## Prerequisites
- CMake 3.15 or later
- Visual Studio 2019 or later (with C++ support)
- FFmpeg 7.1 (provided in `third_party`)

## Build Instructions

1.  Open a command prompt (preferably "x64 Native Tools Command Prompt for VS 2019" or similar).
2.  Navigate to the project directory.
3.  Run `build.bat` OR execute the following manually:

```cmd
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Usage

```cmd
.\build\Release\media_processor.exe data\test.mp4 data\output.mp4
```
