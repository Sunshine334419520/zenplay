# 暂停/恢复同步设计

## 问题背景

在音视频播放器中，暂停/恢复是一个看似简单但实际上容易出错的功能。核心问题是：**暂停期间，时钟如何处理？**

### 🔴 原始设计的问题

#### 场景重现

```cpp
// 用户操作：播放 → 暂停5秒 → 恢复

// 播放时：
video_pts = 1000ms
master_clock = 1000ms
delay = 1000 - 1000 = 0ms ✅ 正常显示

// 暂停5秒后恢复：
video_pts = 1000ms  // 还是同一帧
master_clock = 6000ms  // ❌ 时钟继续推进了！
delay = 1000 - 6000 = -5000ms  // ❌ 严重落后！

// 结果：视频player会疯狂丢帧，试图追赶音频时钟
```

#### 根本原因

`ClockInfo::GetCurrentTime()` 的时钟推算公式：

```cpp
// ❌ 错误的实现
double GetCurrentTime(time_point now) const {
    auto elapsed_ms = (now - system_time).count();  // 包含了暂停时间！
    return pts_ms + elapsed_ms + drift;
}
```

**问题**：`elapsed_ms` 包含了暂停期间的时间，导致时钟在暂停时仍然增长。

## 解决方案

### 核心思路

**在 `AVSyncController` 中统一管理暂停状态**，让所有时钟推算自动排除暂停时间。

### 为什么放在 `AVSyncController`？

| 方案 | 优点 | 缺点 |
|------|------|------|
| `AVSyncController` | 集中管理，自动正确，代码简洁 | 需要传递暂停状态 |
| `VideoPlayer` 单独处理 | 局部独立 | 代码重复，与audio不一致 |
| `AudioPlayer` 单独处理 | 局部独立 | 代码重复，与video不一致 |

**选择方案1**的理由：
1. **时钟推算是同步的核心职责**：暂停影响所有时钟，应该集中管理
2. **避免代码重复**：Video和Audio都需要相同的逻辑
3. **保证一致性**：音频和视频使用同一套暂停计算
4. **简化调用者**：Video/Audio只需简单调用，无需自己处理暂停

## 设计实现

### 1. 数据结构

```cpp
class AVSyncController {
private:
  // 暂停状态管理
  mutable std::mutex pause_mutex_;
  bool is_paused_{false};
  time_point pause_start_time_;
  duration accumulated_pause_duration_;
};
```

### 2. 核心接口

#### `Pause()` - 暂停同步

```cpp
void AVSyncController::Pause() {
  std::lock_guard<std::mutex> lock(pause_mutex_);
  
  if (is_paused_) {
    return;  // 已经暂停，避免重复
  }

  is_paused_ = true;
  pause_start_time_ = now();  // 记录暂停开始时间
}
```

#### `Resume()` - 恢复同步

```cpp
void AVSyncController::Resume() {
  std::lock_guard<std::mutex> lock(pause_mutex_);
  
  if (!is_paused_) {
    return;  // 未暂停，无需恢复
  }

  auto resume_time = now();
  
  // 累计本次暂停时长
  accumulated_pause_duration_ += resume_time - pause_start_time_;
  
  is_paused_ = false;
  pause_start_time_ = {};
}
```

#### `GetAccumulatedPauseDuration()` - 获取累计暂停时长

```cpp
double GetAccumulatedPauseDuration(time_point current_time) const {
  double total_paused_ms = accumulated_pause_duration_.count();
  
  // 如果当前正在暂停，加上本次暂停的时长
  if (is_paused_ && pause_start_time_ != {}) {
    total_paused_ms += (current_time - pause_start_time_).count();
  }
  
  return total_paused_ms;
}
```

### 3. 修改时钟推算

#### 更新 `ClockInfo::GetCurrentTime()`

```cpp
// ✅ 正确的实现 - 排除暂停时间
double GetCurrentTime(time_point now, double paused_duration_ms = 0.0) const {
  auto elapsed_ms = (now - system_time).count();
  double effective_elapsed_ms = elapsed_ms - paused_duration_ms;  // 减去暂停时间
  return pts_ms + effective_elapsed_ms + drift;
}
```

#### 更新 `GetMasterClock()`

```cpp
double AVSyncController::GetMasterClock(time_point current_time) const {
  // 获取累计暂停时长
  double paused_duration_ms = GetAccumulatedPauseDuration(current_time);
  
  switch (sync_mode_) {
    case AUDIO_MASTER:
      return audio_clock_.GetCurrentTime(current_time, paused_duration_ms);
    case VIDEO_MASTER:
      return video_clock_.GetCurrentTime(current_time, paused_duration_ms);
    case EXTERNAL_MASTER:
      auto elapsed_ms = (current_time - play_start_time_).count();
      return elapsed_ms - paused_duration_ms;  // 减去暂停时间
  }
}
```

### 4. 集成到播放控制流程

#### `PlaybackController::Pause()`

```cpp
void PlaybackController::Pause() {
  // ⚠️ 顺序很重要：先暂停同步控制器
  if (av_sync_controller_) {
    av_sync_controller_->Pause();  // 1️⃣ 停止时钟推算
  }

  // 然后暂停播放器
  if (audio_player_) {
    audio_player_->Pause();  // 2️⃣ 停止音频输出
  }
  if (video_player_) {
    video_player_->Pause();  // 3️⃣ 停止视频渲染
  }
}
```

#### `PlaybackController::Resume()`

```cpp
void PlaybackController::Resume() {
  // ⚠️ 顺序很重要：先恢复播放器
  if (audio_player_) {
    audio_player_->Resume();  // 1️⃣ 恢复音频输出
  }
  if (video_player_) {
    video_player_->Resume();  // 2️⃣ 恢复视频渲染
  }

  // 最后恢复同步控制器
  if (av_sync_controller_) {
    av_sync_controller_->Resume();  // 3️⃣ 累计暂停时长，恢复时钟推算
  }
}
```

## 工作流程

### 完整的暂停/恢复时序

```
时间线：
T0: 播放开始
T1: video_pts=1000ms, master_clock=1000ms, delay=0ms ✅
T2: 用户暂停（暂停开始时间=T2）
T3-T7: 暂停期间（5秒）
T8: 用户恢复
    accumulated_pause_duration += (T8 - T2) = 5000ms
T9: video_pts=1000ms
    master_clock = GetCurrentTime(T9, paused=5000ms)
                 = pts + (T9-T2) - 5000ms
                 = 1000 + 7000 - 5000
                 = 3000ms
    
    但实际上T9应该对应的是T1时刻的延续（因为中间暂停了），
    所以 master_clock 应该约等于 1000ms + (T9-T2-暂停时长)
```

### 详细计算示例

```cpp
// === 初始播放 ===
T0 = 0ms:   play_start_time = T0
T1 = 1000ms: UpdateVideoClock(video_pts=1000ms)
            video_clock.pts_ms = 0  // 归一化后
            video_clock.system_time = T1

// 查询主时钟
T1.5 = 1500ms: GetMasterClock(T1.5)
              paused_duration = 0ms
              elapsed = 1500 - 1000 = 500ms
              effective_elapsed = 500 - 0 = 500ms
              clock = 0 + 500 = 500ms ✅

// === 暂停 ===
T2 = 2000ms: Pause()
            is_paused = true
            pause_start_time = T2 (2000ms)

// 暂停期间查询（应该时钟不增长）
T5 = 5000ms: GetMasterClock(T5)
            paused_duration = accumulated(0) + (5000-2000) = 3000ms
            elapsed = 5000 - 1000 = 4000ms
            effective_elapsed = 4000 - 3000 = 1000ms
            clock = 0 + 1000 = 1000ms ✅ 时钟停在暂停前的位置！

// === 恢复 ===
T7 = 7000ms: Resume()
            accumulated_pause_duration = 0 + (7000-2000) = 5000ms
            is_paused = false

// 恢复后查询（应该从暂停时的位置继续）
T8 = 8000ms: GetMasterClock(T8)
            paused_duration = 5000ms
            elapsed = 8000 - 1000 = 7000ms
            effective_elapsed = 7000 - 5000 = 2000ms
            clock = 0 + 2000 = 2000ms ✅ 正确恢复！

// 继续播放
T9 = 9000ms: UpdateVideoClock(video_pts=3000ms)
            normalized_pts = 3000 - 1000 = 2000ms
            master_clock = GetMasterClock(T9)
                        = 0 + (9000-1000) - 5000 = 3000ms
            
            // ⚠️ 这里有个问题：master_clock=3000ms，但video_pts=2000ms
            // 这是因为视频在暂停期间没有解码新帧
            // 恢复后会有一个短暂的追赶过程
```

## 优势分析

### 1. 自动正确性

```cpp
// ✅ VideoPlayer 不需要关心暂停逻辑
auto delay = av_sync_controller_->CalculateVideoDelay(video_pts, now);
// delay 自动排除了暂停时间，无需手动处理！
```

### 2. 代码简化

```cpp
// ❌ 如果在VideoPlayer中处理（错误示范）
double VideoPlayer::CalculateFrameDisplayTime(...) {
  double elapsed = GetEffectiveElapsedTime(current_time);  // 需要自己排除暂停
  double delay = video_pts - elapsed;
  // ...
}

// ✅ 使用AVSyncController（正确做法）
double VideoPlayer::CalculateFrameDisplayTime(...) {
  double delay = av_sync_controller_->CalculateVideoDelay(video_pts, now);
  // 一行搞定，自动处理暂停！
}
```

### 3. 一致性保证

```cpp
// Video和Audio使用完全相同的暂停处理逻辑
video_delay = av_sync_controller_->CalculateVideoDelay(...);
audio_adjustment = av_sync_controller_->CalculateAudioAdjustment(...);
// 两者的master_clock计算完全一致，保证音视频同步！
```

### 4. 线程安全

```cpp
// 暂停状态使用独立的mutex保护
std::mutex pause_mutex_;  // 专门保护暂停相关状态

// 与时钟mutex分离，减少锁竞争
std::mutex clock_mutex_;  // 保护时钟状态
```

## 边界情况处理

### 1. 重复暂停

```cpp
Pause();  // is_paused = true, pause_start_time = T1
Pause();  // ✅ 检测到已暂停，直接返回，不重复记录
```

### 2. 未暂停时恢复

```cpp
Resume();  // ✅ 检测到未暂停，直接返回，避免错误累计
```

### 3. 暂停后Stop

```cpp
Pause();  // 暂停
Stop();   // Reset() 会清空暂停状态
          // accumulated_pause_duration = 0
```

### 4. 多次暂停累计

```cpp
T1: Pause()  // pause_start = T1
T2: Resume() // accumulated += (T2 - T1)

T3: Pause()  // pause_start = T3
T4: Resume() // accumulated += (T4 - T3)

// accumulated = (T2-T1) + (T4-T3) ✅ 正确累计
```

## 测试验证

### 单元测试

```cpp
TEST(AVSyncControllerTest, PauseResumeClockStops) {
  AVSyncController controller;
  
  // 播放1秒
  auto T1 = now();
  controller.UpdateVideoClock(1000.0, T1);
  
  auto clock1 = controller.GetMasterClock(T1 + 500ms);
  EXPECT_NEAR(clock1, 500.0, 10.0);  // 约500ms
  
  // 暂停
  controller.Pause();
  
  // 暂停3秒后查询，时钟应该不增长
  auto clock2 = controller.GetMasterClock(T1 + 3500ms);
  EXPECT_NEAR(clock2, 500.0, 10.0);  // 还是约500ms ✅
  
  // 恢复
  controller.Resume();
  
  // 恢复后500ms，时钟应该增长500ms
  auto clock3 = controller.GetMasterClock(T1 + 4000ms);
  EXPECT_NEAR(clock3, 1000.0, 10.0);  // 500 + 500 = 1000ms ✅
}
```

### 集成测试

```cpp
TEST(VideoPlayerTest, PauseResumeSync) {
  // 播放视频
  player.Play();
  
  // 等待1秒
  sleep(1000ms);
  auto frame1_pts = GetCurrentFramePTS();  // 约1000ms
  
  // 暂停5秒
  player.Pause();
  sleep(5000ms);
  
  // 恢复
  player.Resume();
  
  // 立即检查：应该显示暂停时的帧
  auto frame2_pts = GetCurrentFramePTS();
  EXPECT_NEAR(frame2_pts, frame1_pts, 100.0);  // 应该接近 ✅
  
  // 再等待1秒，应该继续正常播放
  sleep(1000ms);
  auto frame3_pts = GetCurrentFramePTS();
  EXPECT_NEAR(frame3_pts, frame1_pts + 1000, 100.0);  // 约2000ms ✅
}
```

## 性能考虑

### 锁开销

```cpp
// GetMasterClock 频繁调用（每帧），需要获取暂停时长
double GetMasterClock(time_point now) const {
  std::lock_guard<std::mutex> lock(clock_mutex_);
  
  // 嵌套获取pause_mutex（可能有性能影响）
  {
    std::lock_guard<std::mutex> pause_lock(pause_mutex_);
    paused_duration = GetAccumulatedPauseDuration(now);
  }
  
  return audio_clock_.GetCurrentTime(now, paused_duration);
}
```

**优化建议**：
1. `pause_mutex_` 是独立的，只保护暂停状态，锁持有时间极短
2. `GetAccumulatedPauseDuration` 是简单计算，无额外开销
3. 暂停不是高频操作，累计时长计算代价可接受

### 内存开销

```cpp
// 只增加了3个字段
bool is_paused_;                    // 1 byte
time_point pause_start_time_;       // 16 bytes
duration accumulated_pause_duration_; // 16 bytes
std::mutex pause_mutex_;            // 40 bytes (typical)
// 总计约 73 bytes，可忽略
```

## 总结

### 设计原则

1. **集中管理**：暂停状态统一在 `AVSyncController` 管理
2. **自动正确**：时钟推算自动排除暂停时间
3. **简化调用**：Video/Audio无需关心暂停细节
4. **线程安全**：独立mutex保护暂停状态

### 关键要点

1. **暂停时停止时钟推算**：`GetCurrentTime()` 减去暂停时长
2. **累计暂停时间**：支持多次暂停/恢复
3. **正确的调用顺序**：
   - 暂停：先 `AVSync::Pause()` → 再 `Player::Pause()`
   - 恢复：先 `Player::Resume()` → 再 `AVSync::Resume()`

### 适用场景

- ✅ 视频播放器的暂停/恢复
- ✅ 音频播放器的暂停/恢复
- ✅ 多次暂停的累计时长计算
- ✅ Seek后的时钟重置（保留暂停状态选择）

这个设计完美解决了暂停/恢复场景下的音视频同步问题！
