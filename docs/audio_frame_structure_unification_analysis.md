# AudioPlayer base_audio_pts_ 和统一 Frame 结构体的系统性分析

## 问题1: base_audio_pts_ 和 base_pts_initialized_ 是否还有必要?

### 当前使用情况

**在 AudioPlayer 中**:
```cpp
// audio_player.h
double base_audio_pts_;                  // ← 用于什么?
bool base_pts_initialized_{false};       // ← 初始化标志

// audio_player.cpp - PushFrame()
if (!base_pts_initialized_ && timestamp.pts != AV_NOPTS_VALUE) {
  base_audio_pts_ = timestamp.ToSeconds();  // ← 只在第一帧设置
  base_pts_initialized_ = true;
}

// audio_player.cpp - AudioOutputCallback()
double current_pts_ms = player->base_audio_pts_ * 1000.0;
player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
// ← 每次回调都传递同一个 base_audio_pts_!
```

### 问题分析

#### 1. base_audio_pts_ 的设计意图 (原始)

**原本的想法** (已被否定):
```cpp
// 错误的设计 ❌
base_audio_pts_ = 第一帧的PTS  // T0时刻
// 然后在回调中计算:
elapsed_time = now - audio_start_time_
current_clock = base_audio_pts_ + elapsed_time
```

**为什么这是错误的?**
- 这是在做 AVSyncController 已经做的事 (时钟外推)
- 我们已经删除了这个逻辑!

#### 2. 当前实际情况

**现在的代码**:
```cpp
// AudioOutputCallback (当前实现)
double current_pts_ms = player->base_audio_pts_ * 1000.0;
player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
```

**问题**: 
- ❌ `base_audio_pts_` 在第一帧设置后就**永远不变**
- ❌ 每次回调都传递**同一个值** (第一帧的PTS)
- ❌ 这显然是**错误的**!

**正确的做法应该是**:
- ✅ 每次回调传递**当前播放帧的PTS**
- ✅ PTS 应该随着播放进度**不断更新**

### 与 VideoPlayer 的对比

#### VideoPlayer 的正确实现

```cpp
// video_player.h
struct VideoFrame {
  AVFramePtr frame;             // ← 包含 frame->pts
  FrameTimestamp timestamp;     // ← 包含 timestamp.pts 和 time_base
  std::chrono::steady_clock::time_point receive_time;
};

// video_player.cpp - PushFrame()
auto video_frame = std::make_unique<VideoFrame>(std::move(frame), timestamp);
frame_queue_.push(std::move(video_frame));  // ← 帧和时间戳一起存储!

// video_player.cpp - VideoRenderThread()
while (running) {
  VideoFrame* video_frame = frame_queue_.front();  // 从队列取出
  
  // ✅ 使用当前帧的PTS
  double video_pts_ms = video_frame->timestamp.ToMilliseconds();
  
  // ✅ 更新视频时钟
  if (av_sync_controller_) {
    av_sync_controller_->UpdateVideoClock(video_pts_ms, current_time);
  }
  
  // 渲染当前帧
  renderer_->RenderFrame(video_frame->frame.get());
  
  frame_queue_.pop();  // 移除已处理的帧
}
```

**关键点**:
- ✅ 每个 `VideoFrame` 都**携带自己的 timestamp**
- ✅ 从队列取出时,PTS 信息就在 `video_frame->timestamp` 中
- ✅ 每次 `UpdateVideoClock()` 传递的是**当前渲染帧的PTS**

#### AudioPlayer 的错误实现

```cpp
// audio_player.h
std::queue<AVFramePtr> frame_queue_;  // ← 只存储 AVFrame,没有 timestamp!

// audio_player.cpp - PushFrame()
frame_queue_.push(std::move(frame));  // ← 只存 frame,timestamp 丢失!

// audio_player.cpp - FillAudioBuffer()
AVFrame* audio_frame = frame_queue_.front().get();
// ❌ 问题: audio_frame->pts 在这里,但没有 time_base!
// ❌ timestamp 信息在 PushFrame 时就丢失了!

frame_queue_.pop();

// audio_player.cpp - AudioOutputCallback()
// ❌ 无法获取当前播放帧的PTS
// ❌ 只能使用过时的 base_audio_pts_ (第一帧的PTS)
double current_pts_ms = player->base_audio_pts_ * 1000.0;  // ← 永远是第一帧!
player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
```

**根本问题**:
- ❌ AudioPlayer 的 `frame_queue_` 只存 `AVFramePtr`,不存 `FrameTimestamp`
- ❌ 在 `FillAudioBuffer()` 中无法获取完整的时间戳信息
- ❌ 导致 `UpdateAudioClock()` 无法传递正确的当前PTS

### 结论: base_audio_pts_ 的问题

| 方面 | 当前状态 | 正确状态 |
|------|---------|---------|
| 用途 | 存储第一帧PTS | 应该每次传递当前帧PTS |
| 更新频率 | 只设置一次 | 应该每帧更新 |
| AudioOutputCallback | 传递固定值 | 应该传递当前播放位置的PTS |
| 与VideoPlayer一致性 | ❌ 不一致 | ✅ 应该一致 |

**答案**: 
- `base_audio_pts_` 设计上有问题,不应该存在
- `base_pts_initialized_` 也不再需要
- 应该像 VideoPlayer 一样,每个帧都携带自己的时间戳

---

## 问题2: 如何统一 AudioPlayer 和 VideoPlayer 的 Frame 结构?

### 当前的不一致

#### VideoPlayer (正确)
```cpp
// video_player.h
struct VideoFrame {
  AVFramePtr frame;
  FrameTimestamp timestamp;     // ← 时间戳信息
  std::chrono::steady_clock::time_point receive_time;
};

std::queue<std::unique_ptr<VideoFrame>> frame_queue_;  // ← 完整信息
```

#### AudioPlayer (错误)
```cpp
// audio_player.h
std::queue<AVFramePtr> frame_queue_;  // ← 只有 frame,没有 timestamp!
```

### 解决方案: 创建通用的 MediaFrame 结构

#### 步骤1: 在 common_def.h 中定义通用结构

```cpp
// player/common/common_def.h

/**
 * @brief 媒体帧时间戳信息 (音频和视频通用)
 */
struct MediaTimestamp {
  int64_t pts = AV_NOPTS_VALUE;      // 显示时间戳
  int64_t dts = AV_NOPTS_VALUE;      // 解码时间戳
  AVRational time_base{1, 1000000};  // 时间基准

  // 转换为毫秒
  double ToMilliseconds() const {
    if (pts == AV_NOPTS_VALUE || pts < 0) {
      return -1.0;
    }
    return pts * av_q2d(time_base) * 1000.0;
  }

  // 转换为秒
  double ToSeconds() const {
    if (pts == AV_NOPTS_VALUE || pts < 0) {
      return -1.0;
    }
    return pts * av_q2d(time_base);
  }
};

/**
 * @brief 媒体帧信息 (音频和视频通用)
 */
struct MediaFrame {
  AVFramePtr frame;                  // FFmpeg 解码后的帧
  MediaTimestamp timestamp;          // 时间戳信息
  std::chrono::steady_clock::time_point receive_time;  // 接收时间

  MediaFrame(AVFramePtr f, const MediaTimestamp& ts)
      : frame(std::move(f)),
        timestamp(ts),
        receive_time(std::chrono::steady_clock::now()) {}
};
```

#### 步骤2: AudioPlayer 使用 MediaFrame

```cpp
// audio_player.h
class AudioPlayer {
 public:
  // 使用通用的 MediaTimestamp (兼容旧接口)
  using FrameTimestamp = MediaTimestamp;

  // ✅ 修改: 推送帧和时间戳
  bool PushFrame(AVFramePtr frame, const MediaTimestamp& timestamp);

 private:
  // ✅ 修改: 队列存储完整的 MediaFrame
  std::queue<std::unique_ptr<MediaFrame>> frame_queue_;

  // ❌ 删除: 不再需要
  // double base_audio_pts_;
  // bool base_pts_initialized_{false};
};
```

```cpp
// audio_player.cpp
bool AudioPlayer::PushFrame(AVFramePtr frame, const MediaTimestamp& timestamp) {
  if (!frame || state_manager_->ShouldStop()) {
    return false;
  }

  // ❌ 删除: 不再需要设置 base_audio_pts_
  // if (!base_pts_initialized_ && timestamp.pts != AV_NOPTS_VALUE) {
  //   base_audio_pts_ = timestamp.ToSeconds();
  //   base_pts_initialized_ = true;
  // }

  std::lock_guard<std::mutex> lock(frame_queue_mutex_);

  if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
    frame_queue_.pop();
    MODULE_WARN(LOG_MODULE_AUDIO, "Audio queue full, dropping oldest frame");
  }

  // ✅ 修改: 创建 MediaFrame 并入队
  auto media_frame = std::make_unique<MediaFrame>(std::move(frame), timestamp);
  frame_queue_.push(std::move(media_frame));
  frame_available_.notify_one();
  return true;
}
```

#### 步骤3: 修改 FillAudioBuffer 暴露时间戳

```cpp
// audio_player.h
class AudioPlayer {
 private:
  /**
   * @brief 填充音频缓冲区
   * @param buffer 输出缓冲区
   * @param length 缓冲区大小
   * @param current_pts 输出参数:当前播放位置的PTS(毫秒)
   * @return 实际填充的字节数
   */
  size_t FillAudioBuffer(uint8_t* buffer, size_t length, double& current_pts);
};
```

```cpp
// audio_player.cpp
size_t AudioPlayer::FillAudioBuffer(uint8_t* buffer, 
                                    size_t length, 
                                    double& current_pts) {
  size_t filled = 0;
  current_pts = -1.0;  // 默认为无效

  while (filled < length) {
    // 从队列获取帧
    if (frame_queue_.empty()) {
      break;
    }

    MediaFrame* media_frame = frame_queue_.front().get();
    AVFrame* audio_frame = media_frame->frame.get();

    // ✅ 关键: 获取当前帧的PTS
    if (current_pts < 0) {
      current_pts = media_frame->timestamp.ToMilliseconds();
      // 或者根据已播放的采样数计算更精确的PTS
    }

    // 转换音频格式...
    // 拷贝到 internal_buffer_...
    
    frame_queue_.pop();  // 处理完移除
  }

  return filled;
}
```

#### 步骤4: AudioOutputCallback 使用当前PTS

```cpp
// audio_player.cpp
void AudioPlayer::AudioOutputCallback(void* user_data,
                                     uint8_t* stream,
                                     int len) {
  auto* player = static_cast<AudioPlayer*>(user_data);

  double current_pts_ms = -1.0;

  // ✅ 填充缓冲区,同时获取当前PTS
  size_t filled = player->FillAudioBuffer(stream, len, current_pts_ms);

  // ✅ 使用真实的当前PTS更新音频时钟
  if (current_pts_ms >= 0 && player->sync_controller_) {
    auto current_time = std::chrono::steady_clock::now();
    player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
  }

  // 填充剩余部分为静音...
}
```

#### 步骤5: VideoPlayer 改用通用结构

```cpp
// video_player.h
class VideoPlayer {
 public:
  // 使用通用的 MediaTimestamp (兼容旧接口)
  using FrameTimestamp = MediaTimestamp;

 private:
  // ✅ 改用通用的 MediaFrame
  using VideoFrame = MediaFrame;  // type alias

  std::queue<std::unique_ptr<MediaFrame>> frame_queue_;
};
```

### 修改后的架构对比

#### 修改前 (不一致)

```
VideoPlayer:
  queue<VideoFrame>  ← VideoFrame = {AVFramePtr, FrameTimestamp, receive_time}
  UpdateVideoClock(current_frame.timestamp.ToMilliseconds())  ✅

AudioPlayer:
  queue<AVFramePtr>  ← 只有frame,没有timestamp  ❌
  UpdateAudioClock(base_audio_pts_ * 1000.0)  ← 永远是第一帧  ❌
```

#### 修改后 (一致)

```
VideoPlayer:
  queue<MediaFrame>  ← MediaFrame = {AVFramePtr, MediaTimestamp, receive_time}
  UpdateVideoClock(current_frame.timestamp.ToMilliseconds())  ✅

AudioPlayer:
  queue<MediaFrame>  ← MediaFrame = {AVFramePtr, MediaTimestamp, receive_time}
  UpdateAudioClock(current_frame.timestamp.ToMilliseconds())  ✅
```

---

## 实现清单

### 阶段1: 创建通用结构 (common_def.h)

- [ ] 定义 `MediaTimestamp` 结构
- [ ] 定义 `MediaFrame` 结构
- [ ] 添加 `ToMilliseconds()` 和 `ToSeconds()` 方法

### 阶段2: 修改 AudioPlayer

- [ ] 修改 `frame_queue_` 类型为 `queue<unique_ptr<MediaFrame>>`
- [ ] 修改 `PushFrame()` 创建 `MediaFrame` 并入队
- [ ] 删除 `base_audio_pts_` 成员变量
- [ ] 删除 `base_pts_initialized_` 成员变量
- [ ] 修改 `FillAudioBuffer()` 签名,添加 `current_pts` 输出参数
- [ ] 在 `FillAudioBuffer()` 中从 `MediaFrame` 提取 PTS
- [ ] 修改 `AudioOutputCallback()` 使用 `FillAudioBuffer()` 返回的PTS

### 阶段3: 修改 VideoPlayer

- [ ] 将 `FrameTimestamp` 改为 `MediaTimestamp` 的 type alias
- [ ] 将 `VideoFrame` 改为 `MediaFrame` 的 type alias
- [ ] 确保所有使用 `VideoFrame` 的地方仍然工作

### 阶段4: 验证和测试

- [ ] 编译检查无错误
- [ ] 测试音频播放
- [ ] 测试视频播放
- [ ] 测试音视频同步
- [ ] 测试 Seek 后的时间戳

---

## 核心收益

### 1. 正确性
- ✅ AudioPlayer 传递当前播放帧的PTS,而不是第一帧
- ✅ 音频时钟随播放进度正确更新

### 2. 一致性
- ✅ AudioPlayer 和 VideoPlayer 使用相同的数据结构
- ✅ 统一的时间戳管理模式

### 3. 简洁性
- ✅ 删除 `base_audio_pts_` 和 `base_pts_initialized_`
- ✅ 减少冗余状态管理

### 4. 可维护性
- ✅ 通用结构便于后续扩展 (如字幕、元数据)
- ✅ 减少重复代码

---

## 时间戳传递流程对比

### 修改前 (错误)

```
PlaybackController::AudioDecodeTask:
  frame->pts = 100      ───┐
  timestamp.pts = 100      │ 创建 FrameTimestamp
  timestamp.time_base      │
                           │
AudioPlayer::PushFrame(frame, timestamp):
  base_audio_pts_ = 100 (只设置一次)
  queue.push(frame)     ← 只存frame,timestamp丢失!
                           │
FillAudioBuffer():
  frame = queue.front()
  // ❌ 无法获取timestamp!
                           │
AudioOutputCallback():
  current_pts = base_audio_pts_ * 1000  ← 永远是100ms!
  UpdateAudioClock(100.0, now)  ← 错误!
```

### 修改后 (正确)

```
PlaybackController::AudioDecodeTask:
  frame->pts = 100      ───┐
  timestamp.pts = 100      │ 创建 MediaTimestamp
  timestamp.time_base      │
                           │
AudioPlayer::PushFrame(frame, timestamp):
  media_frame = new MediaFrame(frame, timestamp)
  queue.push(media_frame)  ← 帧和时间戳一起存储!
                           │
FillAudioBuffer(&current_pts):
  media_frame = queue.front()
  current_pts = media_frame->timestamp.ToMilliseconds()  ← 获取当前帧PTS!
  // 处理 media_frame->frame
                           │
AudioOutputCallback():
  FillAudioBuffer(buffer, len, current_pts)
  UpdateAudioClock(current_pts, now)  ← 正确的当前PTS!
```

---

## 总结

### 问题根源
1. `base_audio_pts_` 只在第一帧设置,永远不更新
2. `AudioOutputCallback` 传递固定的第一帧PTS,而不是当前PTS
3. AudioPlayer 的 `frame_queue_` 只存 frame,不存 timestamp
4. `FillAudioBuffer` 无法获取当前帧的时间戳信息

### 解决方案
1. 创建通用的 `MediaFrame` 结构 (包含 frame + timestamp)
2. AudioPlayer 的队列存储 `MediaFrame` 而不是 `AVFramePtr`
3. `FillAudioBuffer` 暴露当前帧的PTS
4. `AudioOutputCallback` 使用当前帧的PTS更新时钟
5. 删除 `base_audio_pts_` 和 `base_pts_initialized_`

### 最终效果
- ✅ AudioPlayer 和 VideoPlayer 使用一致的数据结构
- ✅ 音频时钟正确跟踪播放进度
- ✅ 代码更简洁、更易维护
- ✅ 为未来扩展打下基础 (字幕、元数据等)
