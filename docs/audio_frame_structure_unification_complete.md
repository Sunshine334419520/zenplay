# AudioPlayer/VideoPlayer 统一 Frame 结构实现完成报告

## 实现概述

成功将 AudioPlayer 和 VideoPlayer 统一使用通用的 `MediaFrame` 结构,解决了以下问题:
1. ✅ 删除 AudioPlayer 的 `base_audio_pts_` 和 `base_pts_initialized_`
2. ✅ AudioPlayer 队列存储完整的时间戳信息
3. ✅ `FillAudioBuffer` 暴露当前帧的 PTS
4. ✅ `AudioOutputCallback` 传递当前播放帧的 PTS,而不是固定的第一帧 PTS
5. ✅ AudioPlayer 和 VideoPlayer 使用统一的数据结构

---

## 修改文件清单

### 1. `src/player/common/common_def.h` - 定义通用结构

**新增内容**:
```cpp
/**
 * @brief 媒体帧时间戳信息 (音频和视频通用)
 */
struct MediaTimestamp {
  int64_t pts = AV_NOPTS_VALUE;
  int64_t dts = AV_NOPTS_VALUE;
  AVRational time_base{1, 1000000};

  double ToMilliseconds() const { ... }
  double ToSeconds() const { ... }
};

/**
 * @brief 媒体帧信息 (音频和视频通用)
 */
struct MediaFrame {
  AVFramePtr frame;
  MediaTimestamp timestamp;
  std::chrono::steady_clock::time_point receive_time;

  MediaFrame(AVFramePtr f, const MediaTimestamp& ts) : ... {}
};
```

**设计原则**:
- 通用结构,音频和视频共用
- 包含完整的时间戳信息 (pts, dts, time_base)
- 记录接收时间,用于延迟分析

---

### 2. `src/player/audio/audio_player.h` - AudioPlayer 头文件

**修改内容**:

#### 2.1 使用通用 MediaTimestamp
```cpp
// 修改前:
struct FrameTimestamp {
  int64_t pts = AV_NOPTS_VALUE;
  int64_t dts = AV_NOPTS_VALUE;
  AVRational time_base{1, 1000000};
  double ToSeconds() const { ... }
};

// 修改后:
using FrameTimestamp = MediaTimestamp;  // ← 使用通用类型
```

#### 2.2 修改 PushFrame 签名 (已经是正确的)
```cpp
bool PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp);
```

#### 2.3 修改 FillAudioBuffer 签名
```cpp
// 修改前:
int FillAudioBuffer(uint8_t* buffer, int buffer_size);

// 修改后:
int FillAudioBuffer(uint8_t* buffer, int buffer_size, double& current_pts_ms);
//                                                      ↑ 新增输出参数
```

#### 2.4 删除冗余成员变量
```cpp
// 修改前:
double base_audio_pts_;
size_t total_samples_played_;
std::mutex pts_mutex_;
bool base_pts_initialized_{false};
std::queue<AVFramePtr> frame_queue_;  // ← 只存 frame

// 修改后:
size_t total_samples_played_;
std::mutex pts_mutex_;
std::queue<std::unique_ptr<MediaFrame>> frame_queue_;  // ← 存 MediaFrame

// ❌ 删除:
// double base_audio_pts_;
// bool base_pts_initialized_{false};
```

---

### 3. `src/player/audio/audio_player.cpp` - AudioPlayer 实现

#### 3.1 修改 PushFrame 实现
```cpp
bool AudioPlayer::PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp) {
  if (!frame || state_manager_->ShouldStop()) {
    return false;
  }

  // ❌ 删除: base_audio_pts_ 设置逻辑
  // if (!base_pts_initialized_ && timestamp.pts != AV_NOPTS_VALUE) {
  //   base_audio_pts_ = timestamp.ToSeconds();
  //   base_pts_initialized_ = true;
  // }

  std::lock_guard<std::mutex> lock(frame_queue_mutex_);

  if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
    frame_queue_.pop();
  }

  // ✅ 创建 MediaFrame 并入队
  auto media_frame = std::make_unique<MediaFrame>(std::move(frame), timestamp);
  frame_queue_.push(std::move(media_frame));
  frame_available_.notify_one();

  return true;
}
```

#### 3.2 修改 FillAudioBuffer 实现
```cpp
int AudioPlayer::FillAudioBuffer(uint8_t* buffer, 
                                 int buffer_size, 
                                 double& current_pts_ms) {
  current_pts_ms = -1.0;  // 默认为无效

  // ... 暂停/停止检查 ...

  while (bytes_filled < buffer_size) {
    // ... 从内部缓冲区读取 ...

    // 从队列获取帧
    std::unique_lock<std::mutex> lock(frame_queue_mutex_);
    if (frame_queue_.empty()) {
      break;
    }

    // ✅ 获取 MediaFrame (不是 AVFramePtr)
    std::unique_ptr<MediaFrame> media_frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    lock.unlock();

    if (!media_frame || !media_frame->frame) {
      break;
    }

    // ✅ 关键: 提取当前帧的PTS
    if (current_pts_ms < 0) {
      current_pts_ms = media_frame->timestamp.ToMilliseconds();
      MODULE_DEBUG(LOG_MODULE_AUDIO,
                   "Current audio PTS: {:.3f}ms", current_pts_ms);
    }

    AVFrame* frame = media_frame->frame.get();

    // ... 重采样和拷贝数据 ...
  }

  return bytes_filled;
}
```

**关键改进**:
- ✅ 从队列获取 `MediaFrame`,不是裸的 `AVFramePtr`
- ✅ 从 `media_frame->timestamp` 提取 PTS
- ✅ 通过输出参数 `current_pts_ms` 返回 PTS

#### 3.3 修改 AudioOutputCallback 实现
```cpp
int AudioPlayer::AudioOutputCallback(void* user_data,
                                     uint8_t* buffer,
                                     int buffer_size) {
  AudioPlayer* player = static_cast<AudioPlayer*>(user_data);

  double current_pts_ms = -1.0;

  // ✅ FillAudioBuffer 返回当前帧的 PTS
  int bytes_filled = player->FillAudioBuffer(buffer, buffer_size, current_pts_ms);

  // ... 统计更新 ...

  // ✅ 使用当前帧的 PTS 更新音频时钟
  if (bytes_filled > 0 && current_pts_ms >= 0 && player->sync_controller_) {
    auto current_time = std::chrono::steady_clock::now();
    player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
  }

  return bytes_filled;
}
```

**关键改进**:
- ❌ 删除: `player->base_audio_pts_ * 1000.0` (固定值)
- ✅ 使用: `current_pts_ms` (当前帧的真实PTS)

#### 3.4 简化 ClearFrames 和 ResetTimestamps
```cpp
void AudioPlayer::ClearFrames() {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  std::queue<std::unique_ptr<MediaFrame>> empty_queue;  // ← MediaFrame
  frame_queue_.swap(empty_queue);
  buffer_read_pos_ = 0;

  // ❌ 删除 base_audio_pts_ 和 base_pts_initialized_ 重置
  {
    std::lock_guard<std::mutex> pts_lock(pts_mutex_);
    total_samples_played_ = 0;
  }
}

void AudioPlayer::ResetTimestamps() {
  std::lock_guard<std::mutex> lock(pts_mutex_);
  total_samples_played_ = 0;  // ← 只需重置采样计数
  // ❌ 删除 base_audio_pts_ 和 base_pts_initialized_ 重置
}
```

---

### 4. `src/player/video/video_player.h` - VideoPlayer 头文件

**修改内容**:

#### 4.1 使用通用 MediaTimestamp
```cpp
// 修改前:
struct FrameTimestamp {
  int64_t pts = AV_NOPTS_VALUE;
  int64_t dts = AV_NOPTS_VALUE;
  AVRational time_base{1, 1000000};
  double ToMilliseconds() const { ... }
};

// 修改后:
using FrameTimestamp = MediaTimestamp;  // ← type alias
```

#### 4.2 使用通用 MediaFrame
```cpp
// 修改前:
struct VideoFrame {
  AVFramePtr frame;
  FrameTimestamp timestamp;
  std::chrono::steady_clock::time_point receive_time;
  VideoFrame(AVFramePtr f, const FrameTimestamp& ts) : ... {}
};

std::queue<std::unique_ptr<VideoFrame>> frame_queue_;

// 修改后:
using VideoFrame = MediaFrame;  // ← type alias

std::queue<std::unique_ptr<MediaFrame>> frame_queue_;
```

**优点**:
- 使用 type alias 保持接口兼容性
- 内部实现使用通用的 `MediaFrame`
- 无需修改调用代码

---

### 5. `src/player/video/video_player.cpp` - VideoPlayer 实现

**修改内容**:

#### 5.1 修改 PushFrame
```cpp
// 修改前:
auto video_frame = std::make_unique<VideoFrame>(std::move(frame), timestamp);
frame_queue_.push(std::move(video_frame));

// 修改后:
auto media_frame = std::make_unique<MediaFrame>(std::move(frame), timestamp);
frame_queue_.push(std::move(media_frame));
```

#### 5.2 修改 ClearFrames
```cpp
// 修改前:
std::queue<std::unique_ptr<VideoFrame>> empty_queue;

// 修改后:
std::queue<std::unique_ptr<MediaFrame>> empty_queue;
```

**注意**: 由于使用了 type alias `using VideoFrame = MediaFrame;`,其他使用 `VideoFrame&` 或 `const VideoFrame&` 的地方无需修改。

---

## 架构对比

### 修改前 - AudioPlayer (错误设计)

```
PlaybackController::AudioDecodeTask:
  ┌─────────────────────────────────┐
  │ frame->pts = 100                │
  │ timestamp = {100, ..., tb}      │
  └────────────┬────────────────────┘
               │
               ▼
AudioPlayer::PushFrame(frame, timestamp):
  ┌─────────────────────────────────┐
  │ base_audio_pts_ = 100 (一次)    │  ← 只在第一帧设置
  │ queue.push(frame)               │  ← 只存 frame,丢失 timestamp!
  └────────────┬────────────────────┘
               │
               ▼
AudioOutputCallback:
  ┌─────────────────────────────────┐
  │ FillAudioBuffer(buffer)         │
  │   frame = queue.front()         │  ← 只有 frame,没有 timestamp
  │   // ❌ 无法获取当前帧PTS       │
  │                                 │
  │ UpdateAudioClock(               │
  │   base_audio_pts_ * 1000)       │  ← 永远是 100ms! ❌
  └─────────────────────────────────┘
```

### 修改后 - AudioPlayer (正确设计)

```
PlaybackController::AudioDecodeTask:
  ┌─────────────────────────────────┐
  │ frame->pts = 100, 120, 140...   │
  │ timestamp = {pts, dts, tb}      │
  └────────────┬────────────────────┘
               │
               ▼
AudioPlayer::PushFrame(frame, timestamp):
  ┌─────────────────────────────────┐
  │ media_frame = {frame, timestamp}│  ← 创建 MediaFrame
  │ queue.push(media_frame)         │  ← 存储完整信息 ✅
  └────────────┬────────────────────┘
               │
               ▼
AudioOutputCallback:
  ┌─────────────────────────────────┐
  │ FillAudioBuffer(buffer, pts)    │
  │   media_frame = queue.front()   │  ← 获取 MediaFrame
  │   pts = frame.timestamp.ToMs()  │  ← 提取当前帧PTS ✅
  │   // 处理 media_frame.frame     │
  │                                 │
  │ UpdateAudioClock(pts)           │  ← 传递当前帧PTS ✅
  │   // 100ms, 120ms, 140ms...     │
  └─────────────────────────────────┘
```

### VideoPlayer (一直是正确的)

```
PlaybackController::VideoDecodeTask:
  ┌─────────────────────────────────┐
  │ frame->pts = 33, 66, 100...     │
  │ timestamp = {pts, dts, tb}      │
  └────────────┬────────────────────┘
               │
               ▼
VideoPlayer::PushFrame(frame, timestamp):
  ┌─────────────────────────────────┐
  │ media_frame = {frame, timestamp}│  ← 现在也用 MediaFrame
  │ queue.push(media_frame)         │  ← 完整信息 ✅
  └────────────┬────────────────────┘
               │
               ▼
VideoRenderThread:
  ┌─────────────────────────────────┐
  │ media_frame = queue.front()     │  ← 获取 MediaFrame
  │ pts = frame.timestamp.ToMs()    │  ← 提取PTS ✅
  │                                 │
  │ UpdateVideoClock(pts)           │  ← 传递当前帧PTS ✅
  │ RenderFrame(frame)              │
  └─────────────────────────────────┘
```

---

## 核心改进总结

### 1. 数据结构统一

| 组件 | 修改前 | 修改后 |
|------|--------|--------|
| AudioPlayer 队列 | `queue<AVFramePtr>` | `queue<unique_ptr<MediaFrame>>` |
| VideoPlayer 队列 | `queue<unique_ptr<VideoFrame>>` | `queue<unique_ptr<MediaFrame>>` |
| 时间戳结构 | 各自定义 `FrameTimestamp` | 统一使用 `MediaTimestamp` |

### 2. PTS 传递正确性

| 时刻 | AudioPlayer (修改前) | AudioPlayer (修改后) |
|------|---------------------|---------------------|
| T0 (第一帧) | UpdateClock(100ms) | UpdateClock(100ms) ✅ |
| T1 (第二帧) | UpdateClock(100ms) ❌ | UpdateClock(120ms) ✅ |
| T2 (第三帧) | UpdateClock(100ms) ❌ | UpdateClock(140ms) ✅ |

### 3. 成员变量清理

**删除的冗余变量**:
- ❌ `double base_audio_pts_` - 不再需要存储第一帧PTS
- ❌ `bool base_pts_initialized_` - 不再需要初始化标志
- ❌ `AVRational audio_time_base_` - time_base 现在跟随每个帧

**保留的必要变量**:
- ✅ `size_t total_samples_played_` - 用于采样计数(如果需要)
- ✅ `std::mutex pts_mutex_` - 保护 PTS 相关操作

### 4. 接口变化

```cpp
// FillAudioBuffer 新增输出参数
int FillAudioBuffer(uint8_t* buffer, 
                    int buffer_size, 
                    double& current_pts_ms);  // ← 新增
```

---

## 验证清单

### 编译检查
- ✅ `common_def.h` 编译通过
- ✅ `audio_player.h` 编译通过
- ✅ `audio_player.cpp` 编译通过
- ✅ `video_player.h` 编译通过
- ✅ `video_player.cpp` 编译通过
- ✅ `playback_controller.cpp` 编译通过

### 功能验证 (需要运行时测试)
- [ ] 音频播放正常
- [ ] 视频播放正常
- [ ] 音视频同步正确
- [ ] 音频时钟正确更新 (不是固定值)
- [ ] Seek 后时间戳正确
- [ ] 暂停/恢复后时钟正确

### 日志验证
查找日志中的 PTS 更新:
```
"Updating audio clock: current_pts=100.00ms"
"Updating audio clock: current_pts=120.00ms"  ← PTS 应该递增,不是固定值
"Updating audio clock: current_pts=140.00ms"
```

**如果修改前**:
```
"Updating audio clock: base_pts=2.500s (2500.00ms)"  ← 永远是 2.5s
"Updating audio clock: base_pts=2.500s (2500.00ms)"
"Updating audio clock: base_pts=2.500s (2500.00ms)"
```

---

## 后续优化建议

### 1. PTS 精确计算
当前实现在 `FillAudioBuffer` 中只在第一次获取帧时设置 PTS,后续可以根据已播放的采样数计算更精确的 PTS:

```cpp
// 优化方案:
if (current_pts_ms < 0) {
  // 第一次获取时设置基准 PTS
  base_pts_for_this_callback = media_frame->timestamp.ToSeconds();
}

// 根据已播放的采样数计算当前精确位置
double elapsed_seconds = static_cast<double>(total_samples_played_) / sample_rate;
current_pts_ms = (base_pts_for_this_callback + elapsed_seconds) * 1000.0;
```

### 2. DTS 使用
当前实现只使用了 PTS,DTS 信息虽然存储了但未使用。对于某些特殊场景(如B帧),DTS 可能有用。

### 3. 时间戳验证
添加时间戳连续性检查:
```cpp
if (current_pts_ms >= 0 && last_pts_ms >= 0) {
  double pts_delta = current_pts_ms - last_pts_ms;
  if (pts_delta < 0 || pts_delta > 1000.0) {  // 异常跳变
    MODULE_WARN(LOG_MODULE_AUDIO, 
                "PTS discontinuity: {:.2f}ms -> {:.2f}ms (delta={:.2f}ms)",
                last_pts_ms, current_pts_ms, pts_delta);
  }
}
```

---

## 总结

### 问题根源
1. AudioPlayer 只在第一帧设置 `base_audio_pts_`,后续永不更新
2. `AudioOutputCallback` 传递固定的 `base_audio_pts_`,而不是当前播放帧的 PTS
3. AudioPlayer 的队列只存 `AVFramePtr`,时间戳信息丢失
4. 与 VideoPlayer 设计不一致

### 解决方案
1. 创建通用的 `MediaFrame` 结构 (包含 frame + timestamp + receive_time)
2. AudioPlayer 和 VideoPlayer 的队列都存储 `MediaFrame`
3. `FillAudioBuffer` 暴露当前帧的 PTS
4. `AudioOutputCallback` 传递当前帧的 PTS 给 `UpdateAudioClock`
5. 删除 `base_audio_pts_` 和 `base_pts_initialized_`

### 最终效果
- ✅ AudioPlayer 和 VideoPlayer 数据结构统一
- ✅ 音频时钟正确跟踪播放进度
- ✅ PTS 传递从固定值变为动态值
- ✅ 代码更简洁,职责更清晰
- ✅ 为未来扩展打下基础 (字幕、元数据等)
