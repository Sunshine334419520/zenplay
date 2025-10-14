# AudioPlayer 精确 PTS 计算实现完成报告

## 问题回顾

用户发现了 `FillAudioBuffer` 中的一个关键问题:

### 原始设计的困境

```cpp
// 问题场景:
AudioOutputCallback 请求 4096 bytes:
  ├─ 从 internal_buffer_ 读取 2048 bytes (来自 Frame1)
  └─ 从 frame_queue_ 读取 2048 bytes (来自 Frame2)

❓ 应该返回哪个 PTS?
  - Frame1 的 PTS (100ms)? ❌ Frame1 已经播放完了
  - Frame2 的 PTS (120ms)? ❌ Frame2 只播放了一半
  - 平均值 (110ms)? ❌ 不准确,而且比例不一定是 50/50
```

### 核心困难

1. **跨帧填充**: 一个 buffer 可能包含多个 Frame 的片段
2. **Frame 大小不固定**: Frame1 可能是 1024 samples, Frame2 可能是 2048 samples
3. **内部缓冲区**: internal_buffer_ 可能还保留着上次 Frame 的剩余数据
4. **采样率转换**: 重采样后的数据大小与原始 Frame 不对应

---

## 解决方案: 基于采样数的精确 PTS 计算

### 核心思想

不返回"某个 Frame 的 PTS",而是**精确计算当前播放位置的 PTS**:

```
current_pts = base_pts + (samples_played / sample_rate)
```

### 关键组件

#### 1. 维护两个状态变量

```cpp
class AudioPlayer {
 private:
  double current_base_pts_seconds_{0.0};    // 基准 PTS (秒)
  size_t samples_played_since_base_{0};     // 从基准开始已播放的采样数
  int target_sample_rate_{44100};           // 目标采样率
};
```

#### 2. 更新基准 PTS (新帧到来时)

```cpp
// 从队列获取新帧时
std::unique_ptr<MediaFrame> media_frame = frame_queue_.front();

{
  std::lock_guard<std::mutex> lock(pts_mutex_);
  current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();  // ← 更新基准
  samples_played_since_base_ = 0;  // ← 重置计数
}
```

#### 3. 累积播放的采样数 (每次拷贝数据时)

```cpp
// 从 internal_buffer_ 拷贝数据时
int bytes_to_copy = std::min(buffer_size - bytes_filled, available_bytes);
memcpy(buffer + bytes_filled, internal_buffer_.data() + buffer_read_pos_, bytes_to_copy);

{
  std::lock_guard<std::mutex> lock(pts_mutex_);
  int samples_copied = bytes_to_copy / bytes_per_sample;
  samples_played_since_base_ += samples_copied;  // ← 累积采样数
}
```

#### 4. 查询当前 PTS (按需计算)

```cpp
double AudioPlayer::GetCurrentPlaybackPTS() const {
  std::lock_guard<std::mutex> lock(pts_mutex_);

  if (current_base_pts_seconds_ < 0) {
    return -1.0;
  }

  // 根据已播放的采样数计算经过的时间
  double elapsed_seconds = 
      static_cast<double>(samples_played_since_base_) / target_sample_rate_;

  double current_pts_seconds = current_base_pts_seconds_ + elapsed_seconds;

  return current_pts_seconds * 1000.0;  // 转换为毫秒
}
```

---

## 实现详情

### 修改的文件

#### 1. `audio_player.h` - 成员变量

```cpp
// 修改前:
double base_audio_pts_;
size_t total_samples_played_;
bool base_pts_initialized_{false};

// 修改后:
double current_base_pts_seconds_{0.0};
size_t samples_played_since_base_{0};
int target_sample_rate_{44100};
```

**变化**:
- ❌ 删除 `base_audio_pts_` (只存第一帧 PTS,不准确)
- ❌ 删除 `total_samples_played_` (从开始累积,无法重置)
- ❌ 删除 `base_pts_initialized_` (不再需要)
- ✅ 新增 `current_base_pts_seconds_` (当前基准,动态更新)
- ✅ 新增 `samples_played_since_base_` (相对基准的采样数)
- ✅ 新增 `target_sample_rate_` (用于时间换算)

#### 2. `audio_player.h` - 接口修改

```cpp
// 修改前:
int FillAudioBuffer(uint8_t* buffer, int buffer_size, double& current_pts_ms);

// 修改后:
int FillAudioBuffer(uint8_t* buffer, int buffer_size);
double GetCurrentPlaybackPTS() const;  // ← 新增
```

**设计原则**:
- **职责分离**: FillAudioBuffer 只负责填充数据
- **按需查询**: GetCurrentPlaybackPTS 负责 PTS 计算

#### 3. `audio_player.cpp` - Init 保存采样率

```cpp
bool AudioPlayer::Init(const AudioConfig& config) {
  config_ = config;
  target_sample_rate_ = config.target_sample_rate;  // ← 保存用于 PTS 计算
  // ...
}
```

#### 4. `audio_player.cpp` - FillAudioBuffer 累积采样数

```cpp
int AudioPlayer::FillAudioBuffer(uint8_t* buffer, int buffer_size) {
  while (bytes_filled < buffer_size) {
    if (buffer_read_pos_ < internal_buffer_.size()) {
      // 从 internal_buffer_ 读取
      int bytes_to_copy = ...;
      memcpy(...);
      
      // ✅ 累积已播放的采样数
      {
        std::lock_guard<std::mutex> lock(pts_mutex_);
        int samples_copied = bytes_to_copy / bytes_per_sample;
        samples_played_since_base_ += samples_copied;
      }
      
    } else {
      // 从队列获取新帧
      std::unique_ptr<MediaFrame> media_frame = frame_queue_.front();
      
      // ✅ 更新基准 PTS
      {
        std::lock_guard<std::mutex> lock(pts_mutex_);
        current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();
        samples_played_since_base_ = 0;  // 重置
      }
      
      // ... 重采样 ...
    }
  }
  
  return bytes_filled;
}
```

**关键改进**:
- ✅ 每次拷贝数据时累积 `samples_played_since_base_`
- ✅ 新帧到来时更新 `current_base_pts_seconds_` 并重置计数
- ✅ 不再需要在函数内计算和返回 PTS

#### 5. `audio_player.cpp` - GetCurrentPlaybackPTS 新增

```cpp
double AudioPlayer::GetCurrentPlaybackPTS() const {
  std::lock_guard<std::mutex> lock(pts_mutex_);

  if (current_base_pts_seconds_ < 0) {
    return -1.0;
  }

  double elapsed_seconds = 
      static_cast<double>(samples_played_since_base_) / target_sample_rate_;

  double current_pts_seconds = current_base_pts_seconds_ + elapsed_seconds;

  return current_pts_seconds * 1000.0;
}
```

**优点**:
- ✅ 独立的 PTS 查询接口
- ✅ 线程安全 (pts_mutex_ 保护)
- ✅ 可以在任何地方调用

#### 6. `audio_player.cpp` - AudioOutputCallback 使用新接口

```cpp
int AudioPlayer::AudioOutputCallback(void* user_data,
                                     uint8_t* buffer,
                                     int buffer_size) {
  AudioPlayer* player = static_cast<AudioPlayer*>(user_data);

  // ✅ 只负责填充数据
  int bytes_filled = player->FillAudioBuffer(buffer, buffer_size);

  // ✅ 独立查询当前播放位置
  double current_pts_ms = player->GetCurrentPlaybackPTS();

  if (bytes_filled > 0 && current_pts_ms >= 0 && player->sync_controller_) {
    auto current_time = std::chrono::steady_clock::now();
    player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
  }

  return bytes_filled;
}
```

---

## 精确性验证

### 场景1: 跨帧填充

```
Frame1: 1024 samples, PTS = 100ms
Frame2: 1024 samples, PTS = 120ms  
Frame3: 1024 samples, PTS = 140ms
sample_rate = 44100 Hz

Callback1: 请求 4096 bytes = 1024 samples
  - 取 Frame1 (1024 samples)
  - base_pts = 100ms, samples_played = 0
  - 填充后: samples_played = 1024
  - current_pts = 100 + (1024/44100)*1000 = 123.2ms ✅

Callback2: 请求 4096 bytes = 1024 samples
  - 取 Frame2 (1024 samples)
  - base_pts = 120ms, samples_played = 0
  - 填充后: samples_played = 1024
  - current_pts = 120 + (1024/44100)*1000 = 143.2ms ✅
```

### 场景2: 一个 Frame 填充多次

```
Frame1: 8192 samples, PTS = 100ms
sample_rate = 44100 Hz

Callback1: 请求 2048 samples (前 1/4)
  - base_pts = 100ms, samples_played = 0
  - 填充后: samples_played = 2048
  - current_pts = 100 + (2048/44100)*1000 = 146.4ms ✅

Callback2: 请求 2048 samples (第 2/4)
  - base_pts = 100ms (不变), samples_played = 2048
  - 填充后: samples_played = 4096
  - current_pts = 100 + (4096/44100)*1000 = 192.8ms ✅

Callback3: 请求 2048 samples (第 3/4)
  - base_pts = 100ms (不变), samples_played = 4096
  - 填充后: samples_played = 6144
  - current_pts = 100 + (6144/44100)*1000 = 239.3ms ✅

Callback4: 请求 2048 samples (最后 1/4)
  - base_pts = 100ms (不变), samples_played = 6144
  - 填充后: samples_played = 8192
  - current_pts = 100 + (8192/44100)*1000 = 285.7ms ✅
```

### 场景3: 混合场景 (internal_buffer + 新帧)

```
internal_buffer: 512 samples (Frame1 剩余), base_pts = 100ms, samples_played = 1536
Frame2: 2048 samples, PTS = 120ms
sample_rate = 44100 Hz

Callback: 请求 2048 samples
  1. 从 internal_buffer 读取 512 samples
     - samples_played = 1536 + 512 = 2048
     - current_pts = 100 + (2048/44100)*1000 = 146.4ms ✅
  
  2. 从 Frame2 读取 1536 samples
     - base_pts = 120ms (更新), samples_played = 0 (重置)
     - samples_played = 0 + 1536 = 1536
     - current_pts = 120 + (1536/44100)*1000 = 154.8ms ✅

最终返回的 PTS = 154.8ms (Frame2 播放到 1536/2048)
```

---

## 架构优势

### 1. 职责清晰

```
FillAudioBuffer:
  职责: 填充音频数据到 buffer
  行为: 累积 samples_played_since_base_
  
GetCurrentPlaybackPTS:
  职责: 计算当前播放位置的 PTS
  行为: base_pts + samples_played / sample_rate
  
AudioOutputCallback:
  职责: 协调数据填充和时钟更新
  行为: 调用 FillAudioBuffer, 然后调用 GetCurrentPlaybackPTS
```

### 2. 精确性

| 情况 | 旧方案 | 新方案 |
|------|--------|--------|
| 单帧填充 | ✅ 准确 | ✅ 准确 |
| 跨帧填充 | ❌ 只返回第一帧 PTS | ✅ 精确计算当前位置 |
| 一帧多次填充 | ❌ 每次都是同一个 PTS | ✅ 每次递增 |
| internal_buffer 剩余 | ❌ 无法获取 PTS | ✅ 基于采样数计算 |

### 3. 灵活性

```cpp
// 可以在任何地方查询当前播放位置
double current_pts = audio_player->GetCurrentPlaybackPTS();

// 可以用于:
// - 显示当前播放时间
// - 音视频同步
// - 调试日志
// - 统计分析
```

### 4. 鲁棒性

- ✅ 支持采样率转换 (重采样后也准确)
- ✅ 支持 Frame 大小不固定
- ✅ 支持 buffer 大小不固定
- ✅ 线程安全 (pts_mutex_ 保护)

---

## 与 VideoPlayer 的对比

### VideoPlayer (简单)

```cpp
VideoRenderThread:
  video_frame = queue.front();
  
  // ✅ 一帧对应一个完整的图像
  double pts = video_frame->timestamp.ToMilliseconds();
  UpdateVideoClock(pts);
  
  RenderFrame(video_frame->frame);
  queue.pop();
```

**特点**: 
- 一次处理一个完整的帧
- PTS 直接对应帧内容
- 不存在"跨帧"问题

### AudioPlayer (复杂)

```cpp
AudioOutputCallback:
  // ❌ 问题: buffer 大小固定,但 Frame 大小不固定
  // 一个 buffer 可能包含多个 Frame 的片段
  
  FillAudioBuffer(buffer, 4096);
  // ✅ 解决: 累积 samples_played_since_base_
  
  double pts = GetCurrentPlaybackPTS();
  // ✅ 解决: 根据采样数精确计算 PTS
  
  UpdateAudioClock(pts);
```

**特点**:
- 按固定大小的 buffer 请求
- Frame 大小不固定,可能跨 buffer
- 需要精确追踪播放进度

---

## 总结

### 问题根源

AudioOutputCallback 按固定大小的 buffer 请求数据,但 Frame 大小不固定,导致:
1. 一个 buffer 可能包含多个 Frame 的片段
2. internal_buffer_ 可能保留上次 Frame 的剩余数据
3. 无法简单地返回"某个 Frame 的 PTS"

### 解决方案

基于采样数的精确 PTS 计算:
```
current_pts = base_pts + (samples_played / sample_rate)
```

### 关键实现

1. **维护基准**: `current_base_pts_seconds_` (动态更新)
2. **累积采样数**: `samples_played_since_base_` (每次拷贝数据时累加)
3. **查询接口**: `GetCurrentPlaybackPTS()` (按需计算)
4. **职责分离**: FillAudioBuffer 只填充,GetCurrentPlaybackPTS 只计算

### 最终效果

- ✅ 精确追踪播放进度 (采样级精度)
- ✅ 支持跨帧填充
- ✅ 支持采样率转换
- ✅ 线程安全
- ✅ 职责清晰
- ✅ 易于维护和调试

**现在 AudioPlayer 可以像 VideoPlayer 一样,向 AVSyncController 报告精确的当前播放位置!** 🎉
