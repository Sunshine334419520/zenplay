# FillAudioBuffer PTS 返回问题的深度分析

## 问题描述

在 `FillAudioBuffer` 中存在一个复杂的情况:

### 当前的数据流

```
AudioOutputCallback 请求 buffer_size = 4096 bytes
    ↓
FillAudioBuffer:
    ↓
  1. 从 internal_buffer_ 读取 (可能来自上次的 Frame1)
     bytes_filled = 2048
    ↓
  2. internal_buffer_ 用完,从 frame_queue_ 取 Frame2
     bytes_filled = 4096 (包含 Frame1 的剩余 + Frame2 的开始)
    ↓
  返回 current_pts_ms = ??? 
  - Frame1 的 PTS? ❌ Frame1 已经播放完了
  - Frame2 的 PTS? ❌ Frame2 只播放了一部分
  - 平均值? ❌ 不准确
```

### 核心困境

```
buffer 内容:
┌────────────────┬────────────────────────────────┐
│  Frame1 剩余   │       Frame2 开始              │
│  (2048 bytes) │       (2048 bytes)             │
│  PTS=100ms     │       PTS=120ms                │
└────────────────┴────────────────────────────────┘

应该返回哪个 PTS?
1. Frame1 的 100ms? → 不对,Frame1 已经播放完了
2. Frame2 的 120ms? → 不对,Frame2 只播放了一半
3. 中间值 110ms? → 不准确,而且 Frame1 可能只占 10%
4. 加权平均? → 复杂,需要跟踪每个字节来自哪个帧
```

## 问题根源分析

### VideoPlayer 为什么没有这个问题?

**VideoPlayer 的处理方式**:
```cpp
VideoRenderThread:
  while (running) {
    MediaFrame* video_frame = frame_queue_.front();  // 取一帧
    
    double video_pts_ms = video_frame->timestamp.ToMilliseconds();
    UpdateVideoClock(video_pts_ms);  // ← 更新时钟
    
    RenderFrame(video_frame->frame.get());  // ← 渲染整帧
    
    frame_queue_.pop();  // ← 帧完全处理完才移除
  }
```

**关键点**:
- ✅ 一次处理一个完整的帧
- ✅ 帧的 PTS 对应帧的内容
- ✅ 渲染完整帧后才移除

### AudioPlayer 的特殊性

**AudioPlayer 的问题**:
```cpp
AudioOutputCallback 回调请求固定大小的 buffer (如 4096 bytes):
  
  情况1: 一个 Frame 不够填满 buffer
    Frame1 (1024 bytes) → internal_buffer_
    Frame2 (1024 bytes) → internal_buffer_
    Frame3 (1024 bytes) → internal_buffer_
    Frame4 (1024 bytes) → internal_buffer_
    总共 4096 bytes
    → 应该返回哪个 PTS? Frame1? Frame2? Frame3? Frame4?

  情况2: 一个 Frame 超过 buffer
    Frame1 (8192 bytes):
      - 第一次回调: 取 4096 bytes (前半部分)
      - 第二次回调: 取 4096 bytes (后半部分)
    → 两次回调都应该返回 Frame1 的 PTS
    
  情况3: Frame 大小不固定
    Frame1 (2048 bytes) + Frame2 (2048 bytes) = 4096 bytes
    → 返回 Frame1 还是 Frame2 的 PTS?
```

## 解决方案探讨

### 方案1: 返回"正在消费"的第一个帧的 PTS (简单但不准确)

```cpp
int FillAudioBuffer(uint8_t* buffer, int buffer_size, double& current_pts_ms) {
  current_pts_ms = -1.0;
  
  // ... 从 internal_buffer_ 读取 ...
  
  // 从队列获取新帧时设置 PTS
  if (current_pts_ms < 0) {
    current_pts_ms = media_frame->timestamp.ToMilliseconds();
  }
  
  // ... 处理数据 ...
  
  return bytes_filled;
}
```

**问题**:
- ❌ 如果 internal_buffer_ 有数据,current_pts_ms 保持为 -1
- ❌ 如果这次回调没有从队列取新帧,PTS 就是无效的
- ❌ 不能反映真正播放的位置

### 方案2: 跟踪 internal_buffer_ 对应的 PTS (需要额外状态)

```cpp
class AudioPlayer {
 private:
  std::vector<uint8_t> internal_buffer_;
  size_t buffer_read_pos_;
  double internal_buffer_pts_ms_{-1.0};  // ← 新增: internal_buffer_ 的 PTS
  size_t internal_buffer_samples_;       // ← 新增: internal_buffer_ 的采样数
};

int FillAudioBuffer(uint8_t* buffer, int buffer_size, double& current_pts_ms) {
  current_pts_ms = -1.0;
  
  while (bytes_filled < buffer_size) {
    if (buffer_read_pos_ < internal_buffer_.size()) {
      // ✅ 从 internal_buffer_ 读取时,使用其 PTS
      if (current_pts_ms < 0 && internal_buffer_pts_ms_ >= 0) {
        current_pts_ms = internal_buffer_pts_ms_;
      }
      
      // ... 读取数据 ...
    } else {
      // 从队列获取新帧
      MediaFrame* frame = ...;
      
      // ✅ 新帧的 PTS
      if (current_pts_ms < 0) {
        current_pts_ms = frame->timestamp.ToMilliseconds();
      }
      
      // ✅ 存储到 internal_buffer_ 时,记录 PTS
      internal_buffer_pts_ms_ = frame->timestamp.ToMilliseconds();
      internal_buffer_samples_ = frame->frame->nb_samples;
      
      // ... 处理数据 ...
    }
  }
  
  return bytes_filled;
}
```

**优点**:
- ✅ internal_buffer_ 有对应的 PTS
- ✅ 即使没有从队列取新帧,也能返回有效 PTS

**问题**:
- ⚠️ 如果一次回调跨越多个帧,PTS 只是第一个帧的
- ⚠️ 需要额外的成员变量管理

### 方案3: 根据已播放的采样数精确计算 PTS (最准确)

这是**最正确的方案**!

```cpp
class AudioPlayer {
 private:
  // 当前播放位置的基准 PTS (秒)
  double current_base_pts_seconds_{0.0};
  
  // 从 current_base_pts_ 开始,已经播放的采样数
  size_t samples_played_since_base_{0};
  
  // 采样率
  int sample_rate_{44100};
};

int FillAudioBuffer(uint8_t* buffer, int buffer_size, double& current_pts_ms) {
  int bytes_per_sample = config_.target_channels * (config_.target_bits_per_sample / 8);
  int bytes_filled = 0;
  
  while (bytes_filled < buffer_size) {
    if (buffer_read_pos_ < internal_buffer_.size()) {
      // 从 internal_buffer_ 读取
      int bytes_to_copy = ...;
      memcpy(...);
      bytes_filled += bytes_to_copy;
      buffer_read_pos_ += bytes_to_copy;
      
      // ✅ 累积已播放的采样数
      int samples_copied = bytes_to_copy / bytes_per_sample;
      samples_played_since_base_ += samples_copied;
      
    } else {
      // 从队列获取新帧
      MediaFrame* media_frame = frame_queue_.front();
      
      // ✅ 新帧到来时,更新基准 PTS
      current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();
      samples_played_since_base_ = 0;  // 重置计数
      
      // ... 重采样并添加到 internal_buffer_ ...
    }
  }
  
  // ✅ 根据基准 PTS + 已播放采样数计算当前 PTS
  double elapsed_seconds = static_cast<double>(samples_played_since_base_) / sample_rate_;
  current_pts_ms = (current_base_pts_seconds_ + elapsed_seconds) * 1000.0;
  
  return bytes_filled;
}
```

**优点**:
- ✅ 精确计算当前播放位置
- ✅ 即使 buffer 跨越多个帧,也能准确反映播放进度
- ✅ 支持采样率转换后的 PTS 计算

**关键公式**:
```
current_pts = base_pts + (samples_played / sample_rate)

例如:
  base_pts = 2.5 秒 (Frame1 的 PTS)
  sample_rate = 44100 Hz
  samples_played = 22050 (已播放半秒)
  
  current_pts = 2.5 + (22050 / 44100) = 2.5 + 0.5 = 3.0 秒 ✅
```

### 方案4: 在 AudioOutputCallback 层面计算 (分离职责)

**思路**: FillAudioBuffer 只负责填充数据,不返回 PTS。在 AudioOutputCallback 中通过单独的方法获取当前播放位置。

```cpp
class AudioPlayer {
 public:
  // 获取当前播放位置的 PTS (毫秒)
  double GetCurrentPlaybackPTS() const {
    std::lock_guard<std::mutex> lock(pts_mutex_);
    
    double elapsed_seconds = 
        static_cast<double>(samples_played_since_base_) / sample_rate_;
    
    return (current_base_pts_seconds_ + elapsed_seconds) * 1000.0;
  }
  
 private:
  double current_base_pts_seconds_{0.0};
  size_t samples_played_since_base_{0};
  int sample_rate_{44100};
};

int AudioPlayer::FillAudioBuffer(uint8_t* buffer, int buffer_size) {
  // ✅ 只负责填充数据,更新 samples_played_since_base_
  // 不返回 PTS
  
  while (bytes_filled < buffer_size) {
    if (buffer_read_pos_ < internal_buffer_.size()) {
      int bytes_to_copy = ...;
      memcpy(...);
      
      int samples = bytes_to_copy / bytes_per_sample;
      samples_played_since_base_ += samples;  // ← 累积采样数
      
    } else {
      MediaFrame* frame = frame_queue_.front();
      
      // ✅ 更新基准 PTS
      current_base_pts_seconds_ = frame->timestamp.ToSeconds();
      samples_played_since_base_ = 0;
      
      // ... 处理数据 ...
    }
  }
  
  return bytes_filled;
}

int AudioPlayer::AudioOutputCallback(void* user_data, 
                                     uint8_t* buffer, 
                                     int buffer_size) {
  AudioPlayer* player = static_cast<AudioPlayer*>(user_data);
  
  int bytes_filled = player->FillAudioBuffer(buffer, buffer_size);
  
  // ✅ 填充完成后,查询当前播放位置
  double current_pts_ms = player->GetCurrentPlaybackPTS();
  
  if (bytes_filled > 0 && current_pts_ms >= 0 && player->sync_controller_) {
    auto current_time = std::chrono::steady_clock::now();
    player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
  }
  
  return bytes_filled;
}
```

**优点**:
- ✅ 职责分离: FillAudioBuffer 只管填充,GetCurrentPlaybackPTS 只管 PTS
- ✅ PTS 计算独立,可以在其他地方复用
- ✅ 线程安全 (通过 mutex 保护)

## 推荐方案对比

| 方案 | 准确性 | 复杂度 | 跨帧处理 | 推荐度 |
|------|--------|--------|----------|--------|
| 方案1: 返回第一个帧PTS | ❌ 低 | ✅ 简单 | ❌ 不支持 | ⭐ |
| 方案2: 跟踪buffer PTS | ⚠️ 中 | ⚠️ 中 | ⚠️ 部分支持 | ⭐⭐ |
| 方案3: 采样数计算PTS | ✅ 高 | ⚠️ 中 | ✅ 完全支持 | ⭐⭐⭐⭐ |
| 方案4: 分离PTS查询 | ✅ 高 | ✅ 简单 | ✅ 完全支持 | ⭐⭐⭐⭐⭐ |

## 最佳实践: 方案4 的完整实现

### 步骤1: 添加成员变量

```cpp
// audio_player.h
class AudioPlayer {
 private:
  // PTS 跟踪 (基于采样数的精确计算)
  mutable std::mutex pts_mutex_;
  double current_base_pts_seconds_{0.0};    // 当前基准 PTS (秒)
  size_t samples_played_since_base_{0};     // 从基准开始已播放的采样数
  int target_sample_rate_{44100};           // 目标采样率 (从 config_ 获取)
};
```

### 步骤2: 修改 FillAudioBuffer

```cpp
int AudioPlayer::FillAudioBuffer(uint8_t* buffer, int buffer_size) {
  int bytes_per_sample = config_.target_channels * (config_.target_bits_per_sample / 8);
  int bytes_filled = 0;
  
  while (bytes_filled < buffer_size) {
    if (buffer_read_pos_ < internal_buffer_.size()) {
      // 从 internal_buffer_ 读取
      int available_bytes = internal_buffer_.size() - buffer_read_pos_;
      int bytes_to_copy = std::min(buffer_size - bytes_filled, available_bytes);
      
      memcpy(buffer + bytes_filled, 
             internal_buffer_.data() + buffer_read_pos_,
             bytes_to_copy);
      
      bytes_filled += bytes_to_copy;
      buffer_read_pos_ += bytes_to_copy;
      
      // ✅ 累积已播放的采样数
      {
        std::lock_guard<std::mutex> lock(pts_mutex_);
        int samples_copied = bytes_to_copy / bytes_per_sample;
        samples_played_since_base_ += samples_copied;
      }
      
      if (buffer_read_pos_ >= internal_buffer_.size()) {
        internal_buffer_.clear();
        buffer_read_pos_ = 0;
      }
      
      continue;
    }
    
    // 从队列获取新帧
    std::unique_lock<std::mutex> queue_lock(frame_queue_mutex_);
    if (frame_queue_.empty()) {
      break;
    }
    
    std::unique_ptr<MediaFrame> media_frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    queue_lock.unlock();
    
    if (!media_frame || !media_frame->frame) {
      break;
    }
    
    // ✅ 新帧到来,更新基准 PTS
    {
      std::lock_guard<std::mutex> lock(pts_mutex_);
      current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();
      samples_played_since_base_ = 0;  // 重置计数
    }
    
    AVFrame* frame = media_frame->frame.get();
    
    // ... 重采样并添加到 internal_buffer_ ...
  }
  
  return bytes_filled;
}
```

### 步骤3: 添加 GetCurrentPlaybackPTS

```cpp
// audio_player.h
class AudioPlayer {
 public:
  /**
   * @brief 获取当前播放位置的 PTS (毫秒)
   * @return PTS 毫秒数,如果无效返回 -1.0
   */
  double GetCurrentPlaybackPTS() const;
};

// audio_player.cpp
double AudioPlayer::GetCurrentPlaybackPTS() const {
  std::lock_guard<std::mutex> lock(pts_mutex_);
  
  if (current_base_pts_seconds_ < 0) {
    return -1.0;  // 尚未开始播放
  }
  
  // 根据已播放的采样数计算经过的时间
  double elapsed_seconds = 
      static_cast<double>(samples_played_since_base_) / target_sample_rate_;
  
  double current_pts_seconds = current_base_pts_seconds_ + elapsed_seconds;
  
  return current_pts_seconds * 1000.0;  // 转换为毫秒
}
```

### 步骤4: 修改 AudioOutputCallback

```cpp
int AudioPlayer::AudioOutputCallback(void* user_data,
                                     uint8_t* buffer,
                                     int buffer_size) {
  AudioPlayer* player = static_cast<AudioPlayer*>(user_data);
  
  TIMER_START(audio_render);
  int bytes_filled = player->FillAudioBuffer(buffer, buffer_size);
  auto render_time_ms = TIMER_END_MS(audio_render);
  
  // ... 统计更新 ...
  
  // ✅ 查询当前播放位置的 PTS
  double current_pts_ms = player->GetCurrentPlaybackPTS();
  
  if (bytes_filled > 0 && current_pts_ms >= 0 && player->sync_controller_) {
    auto current_time = std::chrono::steady_clock::now();
    player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
  }
  
  return bytes_filled;
}
```

### 步骤5: 在 Init 时保存 sample_rate

```cpp
bool AudioPlayer::Init(const AudioConfig& config) {
  config_ = config;
  target_sample_rate_ = config.target_sample_rate;  // ← 保存采样率
  
  // ... 其他初始化 ...
}
```

## 方案4 的优势总结

### 1. 精确性
```
Frame1: 0-1023 samples, PTS=100ms
Frame2: 1024-2047 samples, PTS=120ms

Callback1: 填充 2048 samples (Frame1 全部 + Frame2 全部)
  base_pts = 100ms
  samples_played = 0 → 2048
  current_pts = 100 + (2048/44100)*1000 = 146.4ms ✅

Callback2: 填充 2048 samples (Frame3 全部 + Frame4 全部)
  base_pts = 140ms (Frame3)
  samples_played = 0 → 2048
  current_pts = 140 + (2048/44100)*1000 = 186.4ms ✅
```

### 2. 鲁棒性
- ✅ 支持跨帧填充
- ✅ 支持帧大小不固定
- ✅ 支持采样率转换后的 PTS 计算

### 3. 可维护性
- ✅ 职责清晰: FillAudioBuffer 负责数据,GetCurrentPlaybackPTS 负责 PTS
- ✅ 线程安全: pts_mutex_ 保护所有 PTS 相关状态
- ✅ 易于调试: 可以随时查询当前播放位置

### 4. 性能
- ✅ 采样数累积是简单的整数加法
- ✅ PTS 计算只在需要时执行 (不是每个循环)
- ✅ 没有复杂的加权平均计算

## 总结

**问题根源**: AudioPlayer 的回调是按固定大小的 buffer 请求的,而 Frame 的大小不固定,一个 buffer 可能包含多个 Frame 的片段。

**最佳方案**: 基于采样数的精确 PTS 计算
- 维护 `current_base_pts_seconds_` (基准 PTS)
- 累积 `samples_played_since_base_` (已播放采样数)
- 计算 `current_pts = base_pts + samples_played / sample_rate`

**关键代码**:
```cpp
// 新帧到来时
current_base_pts_seconds_ = frame->timestamp.ToSeconds();
samples_played_since_base_ = 0;

// 每次拷贝数据时
samples_played_since_base_ += bytes_copied / bytes_per_sample;

// 查询当前 PTS 时
double elapsed = samples_played_since_base_ / sample_rate_;
double current_pts = current_base_pts_seconds_ + elapsed;
```

这样可以精确追踪播放进度,无论 buffer 如何跨帧填充! ✅
