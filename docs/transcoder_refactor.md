# Transcoder 重构记录

## 重构目标

将原有的单文件 `transcoder.cpp/h` 拆分为模块化的 FFmpeg 封装组件，提高代码可维护性和复用性。

## 模块划分

| 文件 | 功能 |
|------|------|
| `demuxer.h/cpp` | 封装输入：打开文件、读取 packet、获取流信息 |
| `video_decoder.h/cpp` | 封装视频解码：支持软解码和硬解码（CUDA/QSV/AMF） |
| `video_encoder.h/cpp` | 封装视频编码：自动选择编码器、支持像素格式转换 |
| `muxer.h/cpp` | 封装输出：创建文件、写入 header/trailer、处理时间戳 |
| `transcoder.h/cpp` | 整合各组件，协调转码流程 |

## 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        Transcoder                           │
│  ┌─────────┐   ┌──────────────┐   ┌──────────────┐   ┌─────┐│
│  │ Demuxer │──▶│VideoDecoder  │──▶│VideoEncoder  │──▶│Muxer││
│  └─────────┘   └──────────────┘   └──────────────┘   └─────┘│
│       │                                                    │ │
│       │              (音频暂未实现)                          │
│       └────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

## 问题修复记录

### 1. MKV 格式写入失败

**现象**：`avformat_write_header` 返回错误 `Invalid data found when processing input`

**根因**：
1. `avformat_new_stream` 创建的流 `time_base` 默认为 `0/0`（无效值）
2. 编码器未设置 `AV_CODEC_FLAG_GLOBAL_HEADER` 标志，导致缺少 extradata（VPS/SPS/PPS）

**修复**：
```cpp
// muxer.cpp - addStream 中设置 time_base
stream->time_base = codecCtx->time_base;

// video_encoder.cpp - 设置全局头标志
tempCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
```

### 2. 硬件解码失败后重试时资源未释放

**现象**：硬件解码失败后，软件解码重试时输出文件被占用

**根因**：`JobManager::processJob` 中第一次创建的 `Transcoder` 对象在重试时未析构

**修复**：
```cpp
// job_system.cpp - 使用作用域确保对象析构
{
    Transcoder transcoder;
    success = transcoder.run(...);
}  // transcoder 在此析构，释放文件句柄

if (!success) {
    Transcoder softwareTranscoder;
    success = softwareTranscoder.run(...);
}
```

### 3. 输出文件路径问题

**现象**：相对路径 `../data/xxx.mkv` 无法创建文件

**修复**：在 `Muxer::open` 中将相对路径转换为绝对路径
```cpp
std::string absPath = GetAbsolutePath(outputPath);
```

### 4. 文件已存在时无法覆盖

**现象**：目标文件被其他进程占用时无法删除

**修复**：在 `JobManager::processJob` 开始时查找可用文件名
```cpp
job->outputPath = findAvailablePath(job->outputPath);
// 如果文件存在，自动添加 _1, _2 等后缀
```

### 5. 多线程竞态问题

**现象**：UI 版本同时运行多个转码任务时出现混乱

**修复**：在 `Transcoder::run` 开始时清理所有资源
```cpp
demuxer_.close();
muxer_.close();
videoDecoder_.close();
videoEncoder_.close();
```

## 当前限制

1. **音频未实现**：当前版本跳过音频，输出文件只有视频轨道
2. **仅支持 H.264 → H.265**：硬编码为 H.265 编码

## 后续优化方向

1. 实现音频解码和重新编码（AAC）
2. 支持更多编码格式
3. 添加错误处理和日志系统
4. 支持视频滤镜（裁剪、缩放等）
