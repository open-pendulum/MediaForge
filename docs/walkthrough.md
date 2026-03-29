# Video Splitter Enhancements Walkthrough

## Overview
Enhanced the video splitter with output directory selection and flexible export modes (separate files or merged output).

## New Features

### 1. Output Directory Selection
- **Default Behavior**: Automatically sets output directory to the source video's directory
- **UI**: "Browse..." button allows changing outputdirectory
- **Display**: Shows current output directory path in UI

### 2. Export Mode Dialog
Replaced direct export with modal dialog offering two modes:

**Separate Files Mode**:
- Exports selected segments as individual files
- Each file named: `{original}_{start-time}_to_{end-time}.{ext}`

**Merge Mode**:
- Combines selected segments into one file
- User can specify custom output filename
- Uses FFmpeg concat demuxer for lossless merge

### 3. Merge Export Implementation

**Algorithm** (`VideoSplitter::exportSegmentsMerged`):
1. Extract each selected segment to temporary files
2. Create concat list file for FFmpeg
3. Use concat demuxer to merge segments
4. Stream copy to output (no re-encoding)
5. Clean up temporary files

**Technical Details**:
- Temporary files stored in: `%TEMP%/mediaforge_merge/`
- Uses FFmpeg's concat protocol with `safe=0` option
- Preserves original quality (stream copy)

## Code Changes

### Modified Files

**[video_splitter.h](file:///d:/workspace/MediaForge/src/video_splitter.h)**:
- Added `exportSegmentsMerged()` method declaration

**[video_splitter.cpp](file:///d:/workspace/MediaForge/src/video_splitter.cpp)**:
- Implemented `exportSegmentsMerged()` (~240 lines)
- Creates temp directory, extracts segments, builds concat list
- Uses `av_find_input_format("concat")` to merge

**[main.cpp](file:///d:/workspace/MediaForge/src/main.cpp)** - ShowSplitUI:
- Added `outputDirectory` static variable
- Added output directory UI (Browse button + display)
- Replaced "Export" button with "Start Export"
- Added modal popup dialog for export mode selection
- Implemented separate/merge export logic based on user choice

## UI Flow

```
[Start Export] → [Modal Dialog]
                      ↓
    ○ Export as Separate Files
    ● Merge into One File
         └─> [Output filename: ______]
                      ↓
              [Confirm] [Cancel]
```

## Usage Instructions

1. **Set Output Directory** (optional):
   - Default: Source video's directory
   - Click "Browse..." to change

2. **Export**:
   - Click "Start Export" button
   - Choose export mode in dialog
   - If merge: Enter output filename
   - Click "Confirm"

3. **Results**:
   - Separate mode: Multiple files in output directory
   - Merge mode: Single combined file

## Verification

✅ Compiles successfully
✅ UI enhancements implemented
✅ Output directory selection working
✅ Export dialog with radio buttons
✅ Merge export using FFmpeg concat
✅ Proper error handling and cleanup
