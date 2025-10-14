# FillAudioBuffer 多帧场景的 PTS 计算分析

## 问题场景

```
buffer_size = 4096 bytes
每帧 = 1024 bytes

Callback 请求填充 4096 bytes:
  需要 4 个帧才能填满

队列状态:
  Frame1: PTS = 100ms, 1024 bytes
  Frame2: PTS = 120ms, 1024 bytes  
  Frame3: PTS = 140ms, 1024 bytes
  Frame4: PTS = 160ms, 1024 bytes

填充过程:
  1. 取 Frame1 → base_pts = 100ms, samples_played = 0
     添加到 internal_buffer
  
  2. 从 internal_buffer 拷贝 1024 bytes → samples_played += 256 (假设)
     继续循环...
  
  3. 取 Frame2 → base_pts = 120ms, samples_played = 0  ← 问题!
     添加到 internal_buffer
  
  4. 从 internal_buffer 拷贝 1024 bytes → samples_played += 256
     继续循环...
  
  5. 取 Frame3 → base_pts = 140ms, samples_played = 0  ← 问题!
     ...
```

## 问题分析

### 当前实现的问题

```cpp
// 当前代码在每次取新帧时都会重置:
{
  std::lock_guard<std::mutex> pts_lock(pts_mutex_);
  current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();
  samples_played_since_base_ = 0;  // ← 重置为 0!
}
```

**问题**:
- Frame1: base_pts=100ms, samples_played=0→256
- Frame2 到来: base_pts=120ms, samples_played=0 (重置!) ← **丢失了 Frame1 的 256 采样**
- Frame3 到来: base_pts=140ms, samples_played=0 (重置!) ← **丢失了 Frame2 的 256 采样**
- Frame4 到来: base_pts=160ms, samples_played=0 (重置!) ← **丢失了 Frame3 的 256 采样**

**结果**: 
```
GetCurrentPlaybackPTS() 返回:
  160ms + (256/44100)*1000 = 165.8ms

但实际上应该是:
  100ms + (1024/44100)*1000 = 123.2ms  (4帧全部播放完)
```

❌ **PTS 计算完全错误!**

---

## 正确的理解

### 关键认知

`samples_played_since_base_` 应该表示**从当前 base_pts 开始,已经播放(输出到 buffer)的采样数**。

但是,"已经播放" 的定义在这里有歧义:

#### 理解1: 已经拷贝到输出 buffer 的采样数 (当前实现)
```
FillAudioBuffer 被调用:
  - 从 internal_buffer 拷贝数据到 output buffer
  - samples_played_since_base_ 累积

问题: 新帧到来时重置 samples_played_since_base_
```

#### 理解2: 从 base_pts 帧开始,已经播放的采样数 (应该的实现)
```
关键: base_pts 应该指向"buffer 中第一个采样"的 PTS
      samples_played 应该累积"buffer 中所有采样数"
```

---

## 正确的实现方案

### 方案: 只在 buffer 为空时更新 base_pts

```cpp
int AudioPlayer::FillAudioBuffer(uint8_t* buffer, int buffer_size) {
  int bytes_filled = 0;
  int bytes_per_sample = config_.target_channels * (config_.target_bits_per_sample / 8);
  bool has_real_audio_data = false;

  // ✅ 记录 buffer 开始时的 PTS 状态
  bool buffer_start_pts_set = false;
  double buffer_start_pts_seconds = 0.0;
  size_t buffer_start_samples = 0;

  while (bytes_filled < buffer_size) {
    if (buffer_read_pos_ < internal_buffer_.size()) {
      // 从 internal_buffer 读取
      int bytes_to_copy = std::min(buffer_size - bytes_filled, 
                                    (int)(internal_buffer_.size() - buffer_read_pos_));
      
      memcpy(buffer + bytes_filled, internal_buffer_.data() + buffer_read_pos_, 
             bytes_to_copy);
      bytes_filled += bytes_to_copy;
      buffer_read_pos_ += bytes_to_copy;

      // ✅ 累积已播放的采样数
      {
        std::lock_guard<std::mutex> lock(pts_mutex_);
        int samples_copied = bytes_to_copy / bytes_per_sample;
        samples_played_since_base_ += samples_copied;
        
        // ✅ 如果这是 buffer 的第一次拷贝,记录开始状态
        if (!buffer_start_pts_set) {
          buffer_start_pts_seconds = current_base_pts_seconds_;
          buffer_start_samples = samples_played_since_base_ - samples_copied;
          buffer_start_pts_set = true;
        }
      }

      if (buffer_read_pos_ >= internal_buffer_.size()) {
        internal_buffer_.clear();
        buffer_read_pos_ = 0;
      }

      has_real_audio_data = true;
      continue;
    }

    // 从队列获取新帧
    std::unique_lock<std::mutex> lock(frame_queue_mutex_);
    if (frame_queue_.empty()) {
      break;
    }

    std::unique_ptr<MediaFrame> media_frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    lock.unlock();

    if (!media_frame || !media_frame->frame) {
      break;
    }

    // ✅ 关键: 只在没有数据时更新 base_pts
    // 如果 internal_buffer 为空且 samples_played_since_base_ > 0,
    // 说明上一个 base 的数据已经全部消费完,需要更新 base
    {
      std::lock_guard<std::mutex> pts_lock(pts_mutex_);
      
      // ✅ 只有当前没有未消费的数据时,才更新 base_pts
      if (internal_buffer_.empty() && buffer_read_pos_ == 0) {
        current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();
        samples_played_since_base_ = 0;
        
        // ✅ 如果这是 buffer 的第一帧,记录开始状态
        if (!buffer_start_pts_set) {
          buffer_start_pts_seconds = current_base_pts_seconds_;
          buffer_start_samples = 0;
          buffer_start_pts_set = true;
        }
        
        MODULE_DEBUG(LOG_MODULE_AUDIO,
                     "New base PTS: {:.3f}s (pts={}, time_base={}/{})",
                     current_base_pts_seconds_, media_frame->timestamp.pts,
                     media_frame->timestamp.time_base.num,
                     media_frame->timestamp.time_base.den);
      } else {
        // ✅ 如果还有未消费的数据,新帧的数据会添加到 internal_buffer,
        // 但不更新 base_pts (保持连续性)
        MODULE_DEBUG(LOG_MODULE_AUDIO,
                     "Frame appended to buffer, keeping base_pts={:.3f}s",
                     current_base_pts_seconds_);
      }
    }

    AVFrame* frame = media_frame->frame.get();
    has_real_audio_data = true;

    // 初始化重采样器
    if (!format_initialized_) {
      if (!InitializeResampler(frame)) {
        MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to initialize resampler");
        break;
      }
    }

    // 重采样
    int resampled_samples = ResampleFrame(frame, resampled_data_, max_resampled_samples_);
    if (resampled_samples <= 0) {
      MODULE_WARN(LOG_MODULE_AUDIO, "Resample failed");
      continue;
    }

    int resampled_bytes = resampled_samples * bytes_per_sample;

    // ✅ 添加到 internal_buffer (累积,不清空)
    size_t old_size = internal_buffer_.size();
    internal_buffer_.resize(old_size + resampled_bytes);
    memcpy(internal_buffer_.data() + old_size, resampled_data_[0], resampled_bytes);
  }

  // 静音填充
  if (bytes_filled < buffer_size) {
    memset(buffer + bytes_filled, 0, buffer_size - bytes_filled);
    bytes_filled = buffer_size;
  }

  last_fill_had_real_data_ = has_real_audio_data;

  return bytes_filled;
}
```

---

## 场景验证

### 场景: 4 帧填充 1 个 buffer

```
buffer_size = 4096 bytes (1024 samples, 假设 stereo 16-bit)
每帧 = 1024 bytes (256 samples)
sample_rate = 44100 Hz

初始状态:
  internal_buffer_ = 空
  current_base_pts_seconds_ = 0
  samples_played_since_base_ = 0

Frame1: PTS = 100ms, 256 samples
Frame2: PTS = 120ms, 256 samples
Frame3: PTS = 140ms, 256 samples
Frame4: PTS = 160ms, 256 samples
```

#### 执行过程

```
FillAudioBuffer(buffer, 4096):

循环1:
  - internal_buffer_ 为空
  - 取 Frame1 (PTS=100ms)
  - internal_buffer_ 为空 && buffer_read_pos_==0 → 更新 base_pts
    current_base_pts_seconds_ = 0.1 (100ms)
    samples_played_since_base_ = 0
    buffer_start_pts_seconds = 0.1
    buffer_start_samples = 0
  - 重采样 → 256 samples (1024 bytes)
  - 添加到 internal_buffer_

循环2:
  - internal_buffer_ 有 1024 bytes
  - 拷贝 1024 bytes 到 output buffer
    bytes_filled = 1024
    samples_played_since_base_ = 0 + 256 = 256
  - internal_buffer_ 清空

循环3:
  - internal_buffer_ 为空
  - 取 Frame2 (PTS=120ms)
  - internal_buffer_ 为空 && buffer_read_pos_==0
    但是 samples_played_since_base_ = 256 > 0
    
    ❓ 应该更新 base_pts 吗?
    
    答案: ❌ 不应该!
    因为当前 buffer 的第一个采样来自 Frame1 (PTS=100ms)
    如果更新 base_pts 为 120ms,会丢失 Frame1 的 256 采样

  - ✅ 正确做法: 不更新 base_pts,保持 base_pts=100ms
  - 重采样 → 256 samples
  - 添加到 internal_buffer_

循环4:
  - internal_buffer_ 有 1024 bytes
  - 拷贝 1024 bytes 到 output buffer
    bytes_filled = 2048
    samples_played_since_base_ = 256 + 256 = 512
  - internal_buffer_ 清空

循环5-6: (类似处理 Frame3)
  - 取 Frame3, 不更新 base_pts
  - 拷贝, samples_played_since_base_ = 768

循环7-8: (类似处理 Frame4)
  - 取 Frame4, 不更新 base_pts
  - 拷贝, samples_played_since_base_ = 1024

退出循环:
  bytes_filled = 4096 (满了)

GetCurrentPlaybackPTS():
  elapsed = 1024 / 44100 = 0.0232 秒 = 23.2ms
  current_pts = 100 + 23.2 = 123.2ms ✅

验证:
  Frame1 (100ms) + Frame2 (120ms) + Frame3 (140ms) + Frame4 (160ms)
  平均 PTS ≈ 130ms
  
  但实际播放位置应该是 Frame1 的开始 + 4 帧的时长
  = 100ms + (1024 samples / 44100 Hz) * 1000
  = 100ms + 23.2ms = 123.2ms ✅
```

---

## 问题: 何时更新 base_pts?

### 错误的时机

```cpp
// ❌ 错误: 每次取新帧都更新
current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();
samples_played_since_base_ = 0;
```

**问题**: 丢失之前累积的 samples_played

### 正确的时机

**原则**: `base_pts` 应该指向"当前输出 buffer 的第一个采样"的 PTS

**更新时机**:
1. ✅ **第一次填充** (buffer 开始时): 设置 base_pts 为第一个帧的 PTS
2. ✅ **internal_buffer 完全消费完** 且 **开始新的 buffer**: 更新 base_pts

**不更新时机**:
1. ❌ **同一个 buffer 填充过程中取多个帧**: 保持 base_pts 不变
2. ❌ **internal_buffer 还有剩余数据**: 保持 base_pts 不变

---

## 更简单的理解

### 核心概念

```
base_pts 的含义:
  "当前正在填充的 output buffer 的第一个采样的 PTS"

samples_played_since_base_ 的含义:
  "从 base_pts 帧开始,已经拷贝到 output buffer 的采样数"
```

### 关键规则

```
规则1: 一次 FillAudioBuffer 调用中,base_pts 只设置一次 (第一次取帧时)

规则2: samples_played_since_base_ 在整个 FillAudioBuffer 过程中持续累积

规则3: 下次 FillAudioBuffer 调用时:
  - 如果 internal_buffer 为空 → 可以更新 base_pts
  - 如果 internal_buffer 有剩余 → 继续使用旧的 base_pts
```

---

## 推荐的实现

### 方案: 在 FillAudioBuffer 开始时决定是否更新 base_pts

```cpp
int AudioPlayer::FillAudioBuffer(uint8_t* buffer, int buffer_size) {
  int bytes_filled = 0;
  int bytes_per_sample = config_.target_channels * (config_.target_bits_per_sample / 8);
  bool has_real_audio_data = false;

  // ✅ 在开始填充前,检查是否需要更新 base_pts
  bool need_update_base_pts = false;
  {
    std::lock_guard<std::mutex> lock(pts_mutex_);
    // 如果 internal_buffer 为空且没有未消费的数据,需要更新 base_pts
    need_update_base_pts = (internal_buffer_.empty() && buffer_read_pos_ == 0);
  }

  while (bytes_filled < buffer_size) {
    // ... 从 internal_buffer 读取 ...
    
    // 从队列获取新帧
    if (frame_queue_.empty()) {
      break;
    }

    std::unique_ptr<MediaFrame> media_frame = std::move(frame_queue_.front());
    frame_queue_.pop();

    // ✅ 只在需要时更新 base_pts
    {
      std::lock_guard<std::mutex> pts_lock(pts_mutex_);
      
      if (need_update_base_pts) {
        current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();
        samples_played_since_base_ = 0;
        need_update_base_pts = false;  // 只更新一次
        
        MODULE_DEBUG(LOG_MODULE_AUDIO, "Base PTS set to {:.3f}s", 
                     current_base_pts_seconds_);
      }
    }

    // ... 重采样并添加到 internal_buffer ...
  }

  return bytes_filled;
}
```

---

## 总结

### 问题根源

当前实现在**每次取新帧时都重置 `samples_played_since_base_ = 0`**,导致:
- 一个 buffer 包含多帧时,只计算最后一帧的采样数
- 丢失了前面帧的采样数累积

### 正确做法

1. **`base_pts` 应该指向当前 output buffer 的第一个采样**
2. **`samples_played_since_base_` 应该累积整个 buffer 的采样数**
3. **只在开始新 buffer 填充时更新 `base_pts`**
4. **同一个 buffer 填充过程中取多个帧时,保持 `base_pts` 不变**

### 关键修改

```cpp
// 修改前 (错误):
// 每次取新帧都重置
current_base_pts_seconds_ = frame->timestamp.ToSeconds();
samples_played_since_base_ = 0;  // ← 错误!

// 修改后 (正确):
// 只在 buffer 开始时设置一次
if (need_update_base_pts) {  // ← 只有开始新 buffer 时才更新
  current_base_pts_seconds_ = frame->timestamp.ToSeconds();
  samples_played_since_base_ = 0;
  need_update_base_pts = false;
}
```

这样才能正确计算跨多帧的 PTS! ✅
