# Pause/Resume 调用时序分析和 Bug 修复

## 问题发现

### 1. `GetAccumulatedPauseDuration()` 未被使用 ✅

经过搜索，`GetAccumulatedPauseDuration()` 只在文档中被引用，**实际代码中没有任何调用**。
- 该函数可以安全删除
- 保留 `accumulated_pause_duration_` 成员变量（用于日志记录）

---

### 2. PlaybackController 中的 Pause/Resume 调用时序问题 ⚠️

#### 当前实现（存在问题）

```cpp
void PlaybackController::Pause() {
  // ❌ 问题：先暂停 AVSyncController
  av_sync_controller_->Pause();  // 记录 pause_start_time_
  
  // 然后暂停播放器
  audio_player_->Pause();  // 停止音频输出
  video_player_->Pause();  // VideoPlayer 也记录自己的 pause_start_time_
}

void PlaybackController::Resume() {
  // 先恢复播放器
  audio_player_->Resume();
  video_player_->Resume();  // VideoPlayer 计算自己的暂停时长
  
  // ❌ 问题：后恢复 AVSyncController
  av_sync_controller_->Resume();  // 调整时钟 system_time
}
```

#### 潜在 Bug

**场景**：Pause() 和 Resume() 之间有时间间隔

```
T1 (1000ms): Pause()
  - av_sync_controller_->Pause() 记录 pause_start_time_ = T1
  - audio_player_->Pause() 停止音频（可能需要几毫秒）
  - video_player_->Pause() 记录自己的 pause_start_time_ = T1 + 5ms
  
T2 (5000ms): Resume()
  - audio_player_->Resume()
  - video_player_->Resume() 计算 pause_duration = T2 - (T1+5ms)
  - av_sync_controller_->Resume() 计算 pause_duration = T2 - T1
  
结果：VideoPlayer 和 AVSyncController 的暂停时长不一致！
```

**更严重的问题**：
- `AVSyncController::Resume()` 会调整 `audio_clock_.system_time` 和 `video_clock_.system_time`
- 但 `VideoPlayer::Resume()` 之后可能立即调用 `UpdateVideoClock()`
- 如果 `Resume()` 顺序错误，可能导致时钟不同步

---

### 3. VideoPlayer 重复管理暂停时长 ⚠️

#### 问题代码

`VideoPlayer` 自己维护了一套暂停时长管理：

```cpp
// video_player.h
std::chrono::steady_clock::duration accumulated_pause_duration_;
std::chrono::steady_clock::time_point pause_start_time_;

// video_player.cpp
void VideoPlayer::Pause() {
  pause_start_time_ = std::chrono::steady_clock::now();
}

void VideoPlayer::Resume() {
  accumulated_pause_duration_ += resume_time - pause_start_time_;
}
```

但这个 `accumulated_pause_duration_` **没有被使用**，因为：
- `AVSyncController` 已经统一管理暂停时长
- `UpdateVideoClock()` 会调用 `AVSyncController`，不使用 `VideoPlayer` 的暂停数据

#### 检查使用情况

```cpp
// video_player.cpp:273
paused_duration_snapshot = accumulated_pause_duration_;
```

这只是保存快照，但后续**没有使用这个快照做任何计算**！

**结论**：`VideoPlayer` 的暂停时长管理是**冗余的**，应该删除。

---

## 修复方案

### 方案 1：调整 PlaybackController 的调用顺序（推荐）

确保暂停/恢复的原子性：

```cpp
void PlaybackController::Pause() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Pausing PlaybackController");

  // ✅ 步骤 1：先暂停音视频播放（停止数据流）
  if (audio_player_) {
    audio_player_->Pause();  // 停止音频输出
  }
  if (video_player_) {
    video_player_->Pause();  // 停止视频渲染（通过 state_manager）
  }

  // ✅ 步骤 2：再暂停同步控制器（记录暂停时间点）
  // 此时音视频已经停止，不会再调用 UpdateClock
  if (av_sync_controller_) {
    av_sync_controller_->Pause();
  }
}

void PlaybackController::Resume() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Resuming PlaybackController");

  // ✅ 步骤 1：先恢复同步控制器（调整时钟基准）
  if (av_sync_controller_) {
    av_sync_controller_->Resume();
  }

  // ✅ 步骤 2：再恢复音视频播放（开始数据流）
  // 此时时钟已经调整好，UpdateClock 会使用正确的 system_time
  if (audio_player_) {
    audio_player_->Resume();
  }
  if (video_player_) {
    video_player_->Resume();
  }
}
```

#### 原理说明

**Pause 顺序**：先停播放，后停时钟
- 原因：确保暂停时钟时，不会有新的 `UpdateClock` 调用
- 即使 `audio_player_->Pause()` 需要几毫秒，也不影响（因为还没记录 `pause_start_time_`）

**Resume 顺序**：先调时钟，后启播放
- 原因：确保播放器启动后，`UpdateClock` 使用的是调整后的 `system_time`
- 时序：`Resume()` 调整时钟 → `AudioPlayer::Resume()` → 音频回调 → `UpdateAudioClock()`

---

### 方案 2：删除 VideoPlayer 的冗余暂停管理

`VideoPlayer` 不需要自己管理暂停时长，因为：
1. 暂停/恢复由 `PlayerStateManager` 统一管理（通过 `ShouldPause()` 和 `WaitForResume()`）
2. 暂停时长由 `AVSyncController` 统一管理
3. `VideoPlayer` 的 `accumulated_pause_duration_` **从未被实际使用**

#### 需要删除的代码

**video_player.h**:
```cpp
// 删除这些成员变量
std::chrono::steady_clock::time_point pause_start_time_;
std::chrono::steady_clock::duration accumulated_pause_duration_;
mutable std::mutex pause_mutex_;
```

**video_player.cpp**:
```cpp
// 简化 Pause/Resume
void VideoPlayer::Pause() {
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer paused");
  // 不需要记录时间，由 PlayerStateManager 和 AVSyncController 管理
}

void VideoPlayer::Resume() {
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer resumed");
  frame_available_.notify_all();  // 唤醒渲染线程
}

// 删除 Stop/Start 中的暂停时长计算
void VideoPlayer::Stop() {
  // 删除：accumulated_pause_duration_ += stop_time - pause_start_time_;
}

void VideoPlayer::Start() {
  // 删除：accumulated_pause_duration_ = ...;
}
```

---

### 方案 3：删除 `GetAccumulatedPauseDuration()` ✅

该函数从未被调用，可以安全删除：

**av_sync_controller.h**:
```cpp
// 删除这个函数声明
double GetAccumulatedPauseDuration(
    std::chrono::steady_clock::time_point current_time = 
        std::chrono::steady_clock::now()) const;
```

**保留** `accumulated_pause_duration_` 成员变量用于日志记录：
```cpp
// 在 Resume() 中用于日志
auto total_paused_ms =
    std::chrono::duration<double, std::milli>(accumulated_pause_duration_).count();
MODULE_INFO(LOG_MODULE_SYNC, "Total paused: {:.2f}ms", total_paused_ms);
```

---

## 详细时序分析

### 修复后的正确时序

#### Pause 流程

```
T0: 用户点击暂停
  ↓
PlaybackController::Pause() 被调用
  ↓
步骤 1: audio_player_->Pause()
  ├─ audio_output_->Pause()
  │   └─ audio_client_->Stop()  // WASAPI 停止音频回调
  └─ 日志：AudioPlayer paused
  ↓
步骤 2: video_player_->Pause()
  └─ 日志：VideoPlayer paused
  ↓
步骤 3: av_sync_controller_->Pause()
  ├─ 获取 clock_mutex_ 和 pause_mutex_
  ├─ is_paused_ = true
  ├─ pause_start_time_ = now()  // 记录暂停时间点
  └─ 日志：AVSyncController paused
  ↓
完成：音视频已停止，时钟已暂停
```

**关键点**：
- 音频回调在 `Pause()` 之后停止，不会再调用 `UpdateAudioClock()`
- 渲染线程会在 `WaitForResume()` 中阻塞，不会调用 `UpdateVideoClock()`
- 此时记录 `pause_start_time_` 是安全的

---

#### Resume 流程

```
T1: 用户点击恢复
  ↓
PlaybackController::Resume() 被调用
  ↓
步骤 1: av_sync_controller_->Resume()
  ├─ 获取 clock_mutex_ 和 pause_mutex_
  ├─ pause_duration = now() - pause_start_time_
  ├─ accumulated_pause_duration_ += pause_duration
  ├─ audio_clock_.system_time += pause_duration  // ⚠️ 关键调整
  ├─ video_clock_.system_time += pause_duration  // ⚠️ 关键调整
  ├─ play_start_time_ += pause_duration
  ├─ is_paused_ = false
  └─ 日志：AVSyncController resumed, total paused: Xms
  ↓
步骤 2: audio_player_->Resume()
  ├─ audio_output_->Resume()
  │   └─ audio_client_->Start()  // WASAPI 启动音频回调
  ├─ frame_available_.notify_all()
  └─ 日志：AudioPlayer resumed
  ↓
步骤 3: video_player_->Resume()
  ├─ frame_available_.notify_all()  // 唤醒渲染线程
  └─ 日志：VideoPlayer resumed
  ↓
完成：时钟已调整，音视频已恢复
  ↓
后续：音频回调恢复 → UpdateAudioClock(new_pts, new_system_time)
      渲染线程恢复 → UpdateVideoClock(new_pts, new_system_time)
      
时钟推算：
  GetCurrentTime(now) = pts + (now - system_time_adjusted) + drift
  其中 system_time_adjusted = 原 system_time + pause_duration
  所以实际计算的 elapsed 自动排除了暂停时间！
```

**关键点**：
- `Resume()` 先调整时钟，再启动播放器
- 播放器启动后的第一次 `UpdateClock` 会使用调整后的 `system_time`
- 时钟推算自动正确，无需额外计算暂停时长

---

## 潜在竞态条件分析

### 竞态 1：Pause() 期间仍有 UpdateClock 调用？

**场景**：
```
Thread 1 (主线程):          Thread 2 (音频回调):
audio_player_->Pause()      正在执行 UpdateAudioClock()
  └─ audio_client_->Stop()  ← 停止回调（需要时间）
                            UpdateAudioClock() 完成
av_sync_controller_->Pause()
  └─ 记录 pause_start_time_
```

**分析**：
- `audio_client_->Stop()` 是异步的，可能有延迟
- 但 `UpdateAudioClock()` 内部有 `clock_mutex_` 保护
- `av_sync_controller_->Pause()` 也需要 `clock_mutex_`
- 所以两者不会同时执行

**结论**：✅ 安全，mutex 确保了互斥

---

### 竞态 2：Resume() 后立即 UpdateClock？

**场景**：
```
Thread 1 (主线程):             Thread 2 (渲染线程):
av_sync_controller_->Resume()  WaitForResume() 阻塞中
  └─ 调整 system_time
  └─ is_paused_ = false        ← 状态改变
video_player_->Resume()
  └─ frame_available_.notify()  → 唤醒线程
                                WaitForResume() 返回
                                UpdateVideoClock(pts, now)
                                  └─ GetCurrentTime(now)
                                      使用调整后的 system_time ✅
```

**分析**：
- `Resume()` 先调整时钟，再唤醒线程
- 渲染线程醒来后，`system_time` 已经是调整后的值
- `GetCurrentTime()` 计算的 `elapsed` 自动正确

**结论**：✅ 安全，顺序正确

---

## 修复总结

### 需要修改的文件

1. **av_sync_controller.h**
   - ✅ 删除 `GetAccumulatedPauseDuration()` 函数声明

2. **playback_controller.cpp**
   - ✅ 调整 `Pause()` 顺序：先停播放器，后停时钟
   - ✅ 调整 `Resume()` 顺序：先启时钟，后启播放器

3. **video_player.h** + **video_player.cpp**
   - ✅ 删除冗余的暂停时长管理
   - ✅ 简化 `Pause()/Resume()/Start()/Stop()` 实现

### 不需要修改的文件

- `audio_player.cpp` - 已经是正确的实现（只是调用 `audio_output_`）
- `av_sync_controller.cpp` - `Pause()/Resume()` 实现已经正确

---

## 测试建议

### 测试用例 1：基本暂停/恢复

```
1. 播放视频 5 秒
2. 暂停
3. 等待 3 秒
4. 恢复播放
5. 检查：画面和声音应该从暂停处继续，没有跳帧或音画不同步
```

### 测试用例 2：快速连续暂停/恢复

```
1. 播放视频
2. 连续执行 10 次：暂停 → 等待 100ms → 恢复 → 等待 100ms
3. 检查：累计暂停时长应该约等于 1 秒（10 × 100ms）
4. 检查：音画同步正常
```

### 测试用例 3：暂停期间 Seek

```
1. 播放视频到 5 秒
2. 暂停
3. Seek 到 10 秒
4. 恢复播放
5. 检查：从 10 秒位置正确开始播放
```

---

**文档版本**: v1.0  
**创建时间**: 2025-10-13  
**状态**: 待修复
