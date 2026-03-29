# Video Splitter Implementation

## Goal Description
Implement a video splitting feature that allows users to preview video, set cut points, and export selected segments. The feature includes:
- A video player with playback controls and seekable progress bar
- Cut point management UI
- Segment selection and export functionality

## User Review Required

> [!IMPORTANT]
> **Video Rendering Approach**: We'll use FFmpeg to decode video frames and OpenGL to render them as textures in ImGui. This requires creating OpenGL textures from decoded frames and uploading them each frame during playback.

> [!WARNING]
> **Performance Consideration**: Real-time video decoding and rendering may be CPU/GPU intensive. We'll implement frame skipping if needed to maintain UI responsiveness.

## Proposed Changes

### Core Components

#### [NEW] [video_player.h](file:///d:/workspace/MediaForge/src/video_player.h)
- `VideoPlayer` class for video decoding, playback, and seeking
- Methods:
  - `bool open(const std::string& path)` - Open video file
  - `bool decode()` - Decode next frame
  - `bool seekTo(double timeSeconds)` - Seek to timestamp
  - `void play()` / `void pause()` / `void stop()` - Playback control
  - `AVFrame* getCurrentFrame()` - Get current decoded frame
  - `double getDuration()` - Get total duration
  - `double getCurrentTime()` - Get current playback time
  - `bool isPlaying()` - Playback state

#### [NEW] [video_player.cpp](file:///d:/workspace/MediaForge/src/video_player.cpp)
- Implementation of `VideoPlayer` class
- Uses FFmpeg `avformat` and `avcodec` for decoding
- Thread-safe frame access
- Automatic frame timing for playback

---

#### [NEW] [video_splitter.h](file:///d:/workspace/MediaForge/src/video_splitter.h)
- `VideoSplitter` class for managing cut points and export
- `CutPoint` struct: timestamp and name
- `Segment` struct: start time, end time, name, export flag
- Methods:
  - `void addCutPoint(double time)` - Add cut point
  - `void removeCutPoint(int index)` - Remove cut point
  - `std::vector<Segment> getSegments()` - Get all segments
  - `bool exportSegments(const std::string& inputPath, const std::string& outputDir, const std::vector<Segment>& segments)` - Export selected segments

#### [NEW] [video_splitter.cpp](file:///d:/workspace/MediaForge/src/video_splitter.cpp)
- Implementation of `VideoSplitter` class
- Uses FFmpeg for splitting (stream copy for fast lossless splitting)
- Segment naming: `{original_name}_{HH-mm-ss}_to_{HH-mm-ss}.{ext}`

---

### UI Components

#### [MODIFY] [main.cpp](file:///d:/workspace/MediaForge/src/main.cpp)

**Add VideoSplitterUI function**:
- Left panel (70% width): Video preview area
  - ImGui image widget showing current frame
  - Playback controls: Play/Pause, Stop
  - Progress bar with seek capability
  - Time display: current / total
  
- Right panel (30% width): Cut point management
  - "Add Cut Point" button
  - List of segments with:
    - Segment name (editable)
    - Time range display
    - Export checkbox
    - Delete button
  - "Export Selected" button

**OpenGL Texture Management**:
- Create texture from AVFrame using `glTexImage2D`
- Update texture each frame during playback
- Convert YUV to RGB using `libswscale`

**Update ShowSplitUI**:
- Replace placeholder with `VideoSplitterUI` function
- Handle "Open Video" button to load file
- Manage `VideoPlayer` and `VideoSplitter` instances

---

### CMakeLists.txt

#### [MODIFY] [CMakeLists.txt](file:///d:/workspace/MediaForge/CMakeLists.txt)
- Add `video_player.cpp` and `video_splitter.cpp` to source files

## Verification Plan

### Manual Verification
1. **Open Video**: Select a video file, verify it loads and displays first frame
2. **Playback**: Click Play, verify smooth playback with audio (optional)
3. **Seek**: Drag progress bar, verify video jumps to correct position
4. **Add Cut Points**: 
   - Seek to different positions
   - Click "Add Cut Point" at each position
   - Verify cut points appear in list
5. **Segment Management**:
   - Verify segments are created automatically between cut points
   - Edit segment names
   - Toggle export checkboxes
6. **Export**:
   - Select segments to export
   - Click "Export Selected"
   - Verify output files are created with correct time ranges
   - Verify output files play correctly in media player
