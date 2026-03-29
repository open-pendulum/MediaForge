# MediaForge 重构架构文档

## 概述

MediaForge 已经重构为基于有向无环图(DAG)的节点式架构，每个节点专门负责特定任务，通过通用接口传递数据。

## 核心组件

### 1. MediaData - 数据结构

```cpp
// 支持引用计数的数据基类
class MediaData {
    using Ptr = std::shared_ptr<MediaData>;
    // ...
};

// 具体数据类型
VideoPacketData    // 视频数据包
AudioPacketData    // 音频数据包
VideoFrameData     // 视频帧
AudioSampleData    // 音频样本
ConfigData         // 配置数据
```

### 2. SyncDataPipe - 数据管道

```cpp
// 同步阻塞式数据管道
class SyncDataPipe {
    bool push(MediaData* data);  // 生产者推送
    MediaData* pull();           // 消费者拉取
};
```

### 3. MediaNode - 节点基类

```cpp
class MediaNode {
    bool processData();                    // 处理数据
    bool canProcess(MediaType type) const; // 检查能否处理
    NodeType getNodeType() const;          // 获取节点类型
};
```

### 4. MediaEngine - 媒体引擎

```cpp
class MediaEngine {
    // 节点管理
    template<typename NodeType>
    std::shared_ptr<NodeType> createNode(...);

    // 连接管理
    void connectNodes(const std::string& from, const std::string& to);

    // 执行控制
    void executeGraph(const ExecuteConfig& config);
};
```

## 转码链路示例

### 1. 创建音视频转码链路

```cpp
TranscodeService service;

// 创建完整的音视频转码链路
service.createAVTranscodePipeline(
    "input.mp4",          // 输入文件
    "output.mp4",         // 输出文件
    "libx264",           // 视频编码器
    4000000,             // 视频比特率
    "aac",               // 音频编码器
    128000               // 音频比特率
);

// 开始转码
service.startTranscoding();
```

### 2. 手动执行模式

```cpp
// 创建链路
service.createVideoTranscodePipeline("input.mp4", "output.mp4");

// 手动单步执行
for (int i = 0; i < 100; ++i) {
    service.stepTranscoding();
    // 检查状态...
}
```

### 3. 监控和调试

```cpp
// 设置数据流监控
service.onDataFlow([](const std::string& from, const std::string& to, float progress) {
    std::cout << from << " -> " << to << " : " << progress << std::endl;
});

// 获取统计信息
auto stats = service.getStats();
std::cout << "Progress: " << stats.progress << std::endl;
```

## 节点类型

| 节点类型 | 功能 | 处理数据类型 |
|---------|------|-------------|
| FILE_INPUT | 文件读取 | 无 |
| DECODE_VIDEO | 视频解码 | VIDEO_PACKET |
| DECODE_AUDIO | 音频解码 | AUDIO_PACKET |
| ENCODE_VIDEO | 视频编码 | VIDEO_FRAME |
| ENCODE_AUDIO | 音频编码 | AUDIO_SAMPLE |
| PASSTHROUGH_AUDIO | 音频直通 | AUDIO_PACKET |
| FILE_OUTPUT | 文件写入 | VIDEO/AUDIO_PACKET |

## 执行模式

### 1. SEQUENTIAL（顺序执行）
- 按照拓扑顺序依次执行节点
- 简单可靠，适合调试

### 2. LEVEL_BY_LEVEL（层级执行）
- 同层级节点可以并行执行
- 适合 CPU 密集型任务

### 3. MANUAL（手动模式）
- 通过 `step()` 方法手动控制执行
- 适合调试和精细控制

## 性能优化

### 1. 零拷贝优化
- 使用 `std::shared_ptr` 管理数据生命周期
- 避免不必要的数据拷贝

### 2. 批处理优化
```cpp
ExecuteConfig config;
config.enableBatchProcessing = true;
config.batchSize = 32;
```

### 3. 预取优化
```cpp
config.enablePrefetch = true;
config.prefetchCount = 2;
```

## 扩展新节点

### 1. 继承 MediaNode
```cpp
class CustomFilterNode : public MediaNode {
public:
    bool processData() override {
        // 处理逻辑...
    }

    bool canProcess(MediaType type) const override {
        return type == MediaType::VIDEO_FRAME;
    }

    NodeType getNodeType() const override {
        return NodeType::FILTER_VIDEO;
    }
};
```

### 2. 注册到 MediaEngine
```cpp
engine->createNode<CustomFilterNode>("custom_filter");
```

## 故障排除

### 1. 常见错误
- 循环依赖：检查是否有节点间循环引用
- 内存泄漏：确保正确使用 `std::shared_ptr`
- 性能问题：启用批处理和预取优化

### 2. 调试技巧
- 使用 `visualizeGraph()` 查看DAG结构
- 使用手动模式逐步执行
- 监控数据流事件

## 迁移旧代码

### 1. 保留功能
- 原有的 `Transcoder` 类可以作为 `EncodeNode` 使用
- `JobSystem` 可以集成到 MediaEngine 的执行管理中

### 2. 逐步迁移
1. 先实现新的转码服务
2. 逐步将旧功能迁移到节点模式
3. 保持接口兼容性