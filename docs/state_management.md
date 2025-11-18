# 🔄 状态管理系统设计

> **文档版本**: v1.0  
> **最后更新**: 2025-11-18  
> **相关文档**: [整体架构设计](architecture_overview.md) | [核心组件详解](core_components.md)

---

## 📋 文档概览

本文档详细介绍 ZenPlay 的状态管理系统 `PlayerStateManager`，包括状态机设计、状态转换规则、线程安全实现和使用指南。

**主要内容**:
1. 设计原则与架构
2. 状态定义与转换规则
3. 线程安全实现
4. 状态查询与通知
5. 典型场景与状态流转
6. 最佳实践与常见陷阱

**阅读建议**:
- 首先阅读 [整体架构设计](architecture_overview.md) 了解状态管理器在系统中的位置
- 结合 [核心组件详解](core_components.md) 理解各组件如何使用状态管理器
- 本文档适合需要理解播放器状态流转的开发者

---

## 🎯 设计原则

### 核心原则

PlayerStateManager 的设计遵循以下核心原则：

#### 1. **单一状态源 (Single Source of Truth)**

```cpp
// ❌ 错误：多处状态，容易不一致
class PlaybackController {
  bool is_playing_ = false;  // 这里一个状态
};
class AudioPlayer {
  bool is_playing_ = false;  // 这里又一个状态
};
class VideoPlayer {
  bool is_playing_ = false;  // 这里再一个状态
};

// ✅ 正确：统一的状态管理器
class PlaybackController {
  std::shared_ptr<PlayerStateManager> state_manager_;  // 唯一状态源
};
class AudioPlayer {
  PlayerStateManager* state_manager_;  // 引用（不拥有）
};
class VideoPlayer {
  PlayerStateManager* state_manager_;  // 引用（不拥有）
};
```

**优势**:
- 避免状态不一致
- 状态查询简单直接
- 易于调试和追踪

#### 2. **状态转换原子性**

```cpp
// 使用原子操作确保状态转换的原子性
std::atomic<PlayerState> current_state_;

bool RequestStateChange(PlayerState new_state) {
  PlayerState old_state = current_state_.load(std::memory_order_acquire);
  
  if (!IsValidTransition(old_state, new_state)) {
    return false;  // 非法转换
  }
  
  // CAS 原子操作
  if (!current_state_.compare_exchange_strong(old_state, new_state)) {
    return RequestStateChange(new_state);  // 重试
  }
  
  NotifyStateChange(old_state, new_state);
  return true;
}
```

**优势**:
- 多线程环境下状态转换安全
- 避免竞态条件
- 状态变更可追踪

#### 3. **观察者模式通知**

```cpp
// 注册回调，监听状态变化
int callback_id = state_manager_->RegisterStateChangeCallback(
    [](PlayerState old_state, PlayerState new_state) {
      std::cout << "State changed: " << GetStateName(old_state)
                << " -> " << GetStateName(new_state) << std::endl;
    });

// 取消注册
state_manager_->UnregisterStateChangeCallback(callback_id);
```

**优势**:
- UI 层可实时响应状态变化
- 解耦状态变更通知
- 支持多个观察者

---

## 📊 状态定义

### 状态枚举

```cpp
enum class PlayerState {
  kIdle,       // 空闲（未打开文件）
  kOpening,    // 正在打开文件
  kStopped,    // 已停止（文件已打开但未播放）
  kPlaying,    // 正在播放
  kPaused,     // 已暂停
  kSeeking,    // 正在跳转
  kBuffering,  // 缓冲中（预留，暂未使用）
  kError       // 错误状态
};
```

### 状态详细说明

| 状态 | 含义 | 典型场景 | 可执行操作 |
|------|------|---------|-----------|
| **kIdle** | 播放器空闲，未打开任何文件 | 初始状态、Close() 后 | Open() |
| **kOpening** | 正在打开文件 | Open() 执行中 | 等待完成 |
| **kStopped** | 文件已打开，但未播放 | Open() 成功、Stop() 后 | Play(), Seek(), Close() |
| **kPlaying** | 正在播放 | Play() 后 | Pause(), Stop(), Seek() |
| **kPaused** | 暂停播放 | Pause() 后 | Play(), Stop(), Seek() |
| **kSeeking** | 正在跳转 | SeekAsync() 执行中 | 等待完成 |
| **kBuffering** | 缓冲中（预留） | 网络流缓冲不足 | 等待缓冲 |
| **kError** | 发生错误 | Open() 失败、解码错误 | Close() |

### 状态生命周期

```
┌─────────────────────────────────────────────────────────────┐
│                    播放器状态生命周期                        │
└─────────────────────────────────────────────────────────────┘

         ┌──────────┐
    ┌───│  kIdle   │◄────────────────┐
    │   └──────────┘                  │
    │       │ Open()                  │ Close()
    │       ▼                         │
    │   ┌──────────┐                  │
    │   │kOpening  │──────────────────┤ Open失败
    │   └──────────┘                  │
    │       │ Open成功                │
    │       ▼                         │
    │   ┌──────────┐                  │
    ├──►│kStopped  │◄─────┐           │
    │   └──────────┘      │           │
    │       │ Play()      │ Stop()    │
    │       ▼             │           │
    │   ┌──────────┐      │           │
    ├──►│kPlaying  │──────┤           │
    │   └──────────┘      │           │
    │       │ Pause()     │           │
    │       ▼             │           │
    │   ┌──────────┐      │           │
    ├──►│ kPaused  │──────┘           │
    │   └──────────┘                  │
    │       │ Play()                  │
    │       ├────────────────┐        │
    │       │                │        │
    │   ┌──────────┐         │        │
    └──►│kSeeking  │─────────┘        │
        └──────────┘                  │
            │ Seek完成                │
            └─────────────────────────┘
            
         ┌──────────┐
    ┌───│  kError  │
    │   └──────────┘
    │       │ Close()
    │       ▼
    │   ┌──────────┐
    └──►│  kIdle   │
        └──────────┘
```

---

## 🔀 状态转换规则

### 完整转换规则表

| 当前状态 | 可转换到的状态 | 触发条件 |
|---------|---------------|---------|
| **kIdle** | `kOpening` | `Open()` 开始 |
| **kOpening** | `kStopped` | `Open()` 成功 |
| **kOpening** | `kError` | `Open()` 失败 |
| **kStopped** | `kPlaying` | `Play()` |
| **kStopped** | `kSeeking` | `SeekAsync()` |
| **kStopped** | `kIdle` | `Close()` |
| **kPlaying** | `kPaused` | `Pause()` |
| **kPlaying** | `kStopped` | `Stop()` |
| **kPlaying** | `kSeeking` | `SeekAsync()` |
| **kPlaying** | `kBuffering` | 缓冲不足（预留） |
| **kPlaying** | `kError` | 解码错误 |
| **kPaused** | `kPlaying` | `Play()` / `Resume()` |
| **kPaused** | `kStopped` | `Stop()` |
| **kPaused** | `kSeeking` | `SeekAsync()` |
| **kSeeking** | `kPlaying` | Seek 完成，原状态为 Playing |
| **kSeeking** | `kPaused` | Seek 完成，原状态为 Paused |
| **kSeeking** | `kStopped` | Seek 失败 |
| **kBuffering** | `kPlaying` | 缓冲完成 |
| **kBuffering** | `kError` | 缓冲超时 |
| **kError** | `kIdle` | `Close()` |

### 实现代码

```cpp
bool PlayerStateManager::IsValidTransition(PlayerState from, 
                                           PlayerState to) const {
  switch (from) {
    case PlayerState::kIdle:
      return to == PlayerState::kOpening;
    
    case PlayerState::kOpening:
      return to == PlayerState::kStopped || to == PlayerState::kError;
    
    case PlayerState::kStopped:
      return to == PlayerState::kPlaying || 
             to == PlayerState::kIdle || 
             to == PlayerState::kSeeking;
    
    case PlayerState::kPlaying:
      return to == PlayerState::kPaused || 
             to == PlayerState::kStopped ||
             to == PlayerState::kSeeking || 
             to == PlayerState::kBuffering ||
             to == PlayerState::kError;
    
    case PlayerState::kPaused:
      return to == PlayerState::kPlaying || 
             to == PlayerState::kStopped ||
             to == PlayerState::kSeeking;
    
    case PlayerState::kSeeking:
      return to == PlayerState::kPlaying || 
             to == PlayerState::kPaused ||
             to == PlayerState::kStopped;
    
    case PlayerState::kBuffering:
      return to == PlayerState::kPlaying || to == PlayerState::kError;
    
    case PlayerState::kError:
      return to == PlayerState::kIdle;
    
    default:
      return false;
  }
}
```

---

## 🔒 线程安全实现

### 原子操作保证状态转换安全

```cpp
class PlayerStateManager {
 private:
  // 使用 std::atomic 保证多线程安全
  std::atomic<PlayerState> current_state_;
};

PlayerState PlayerStateManager::GetState() const {
  return current_state_.load(std::memory_order_acquire);
}

bool PlayerStateManager::RequestStateChange(PlayerState new_state) {
  PlayerState old_state = current_state_.load(std::memory_order_acquire);
  
  // 状态相同，无需转换
  if (old_state == new_state) {
    return true;
  }
  
  // 检查转换是否合法
  if (!IsValidTransition(old_state, new_state)) {
    return false;
  }
  
  // CAS 原子操作：比较并交换
  if (!current_state_.compare_exchange_strong(
          old_state, new_state,
          std::memory_order_release,
          std::memory_order_acquire)) {
    // CAS 失败，说明其他线程修改了状态，重试
    return RequestStateChange(new_state);
  }
  
  // 通知观察者
  NotifyStateChange(old_state, new_state);
  
  // ✅ 唤醒等待的线程
  if (new_state == PlayerState::kPlaying ||
      new_state == PlayerState::kStopped ||
      new_state == PlayerState::kIdle ||
      new_state == PlayerState::kError) {
    pause_cv_.notify_all();
  }
  
  return true;
}
```

**关键点**:
- `std::atomic` 确保多线程读写安全
- `compare_exchange_strong` 避免竞态条件
- `memory_order_acquire/release` 保证内存可见性

### 暂停/恢复同步机制

```cpp
class PlayerStateManager {
 private:
  mutable std::mutex pause_mutex_;
  std::condition_variable pause_cv_;
};

bool PlayerStateManager::ShouldPause() const {
  auto state = GetState();
  return state == PlayerState::kPaused || 
         state == PlayerState::kBuffering ||
         state == PlayerState::kSeeking;
}

bool PlayerStateManager::WaitForResume(int timeout_ms) {
  std::unique_lock<std::mutex> lock(pause_mutex_);
  
  auto predicate = [this]() {
    auto state = GetState();
    // 继续执行的条件：正在播放 或 应该停止
    return state == PlayerState::kPlaying || ShouldStop();
  };
  
  if (timeout_ms > 0) {
    return pause_cv_.wait_for(
        lock, std::chrono::milliseconds(timeout_ms), predicate);
  } else {
    pause_cv_.wait(lock, predicate);
    return true;
  }
}
```

**使用示例**:

```cpp
void PlaybackController::DemuxTask() {
  while (!state_manager_->ShouldStop()) {
    // 检查是否需要暂停
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();  // 阻塞直到 Resume()
      continue;
    }
    
    // 正常工作...
    auto packet = demuxer_->ReadPacket();
    video_packet_queue_.Push(packet);
  }
}
```

---

## 📡 状态查询与通知

### 状态查询接口

```cpp
class PlayerStateManager {
 public:
  // ========== 通用查询 ==========
  
  PlayerState GetState() const;
  
  // ========== 便捷查询 ==========
  
  bool IsIdle() const { return GetState() == PlayerState::kIdle; }
  bool IsOpening() const { return GetState() == PlayerState::kOpening; }
  bool IsStopped() const { return GetState() == PlayerState::kStopped; }
  bool IsPlaying() const { return GetState() == PlayerState::kPlaying; }
  bool IsPaused() const { return GetState() == PlayerState::kPaused; }
  bool IsSeeking() const { return GetState() == PlayerState::kSeeking; }
  bool IsBuffering() const { return GetState() == PlayerState::kBuffering; }
  bool IsError() const { return GetState() == PlayerState::kError; }
  
  // ========== 工作线程专用 ==========
  
  /**
   * @brief 检查是否应该停止工作线程
   * @return true 表示线程应该退出
   */
  bool ShouldStop() const {
    auto state = GetState();
    return state == PlayerState::kIdle || 
           state == PlayerState::kStopped ||
           state == PlayerState::kError;
  }
  
  /**
   * @brief 检查是否应该暂停工作
   * @return true 表示应该暂停处理
   */
  bool ShouldPause() const {
    auto state = GetState();
    return state == PlayerState::kPaused || 
           state == PlayerState::kBuffering ||
           state == PlayerState::kSeeking;
  }
};
```

### 状态通知机制

```cpp
using StateChangeCallback = 
    std::function<void(PlayerState old_state, PlayerState new_state)>;

class PlayerStateManager {
 public:
  /**
   * @brief 注册状态变更回调
   * @return 回调 ID，用于取消注册
   */
  int RegisterStateChangeCallback(StateChangeCallback callback);
  
  /**
   * @brief 取消注册回调
   */
  void UnregisterStateChangeCallback(int callback_id);
  
 private:
  void NotifyStateChange(PlayerState old_state, PlayerState new_state);
  
  std::mutex callbacks_mutex_;
  std::vector<std::pair<int, StateChangeCallback>> callbacks_;
  int next_callback_id_ = 0;
};

void PlayerStateManager::NotifyStateChange(PlayerState old_state, 
                                           PlayerState new_state) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  
  for (auto& [id, callback] : callbacks_) {
    if (callback) {
      callback(old_state, new_state);
    }
  }
}
```

**使用示例** (UI 层):

```cpp
// 注册回调，更新 UI
int callback_id = player.RegisterStateChangeCallback(
    [this](PlayerState old_state, PlayerState new_state) {
      // 在 UI 线程更新界面
      QMetaObject::invokeMethod(this, [this, new_state]() {
        if (new_state == PlayerState::kPlaying) {
          playButton->setText("暂停");
        } else if (new_state == PlayerState::kPaused) {
          playButton->setText("播放");
        } else if (new_state == PlayerState::kSeeking) {
          statusLabel->setText("正在跳转...");
        }
      }, Qt::QueuedConnection);
    });
```

---

## 🎬 典型场景与状态流转

### 场景 1: 正常播放流程

```
用户操作                      状态转换
────────                      ────────
Open("video.mp4")       →    kIdle → kOpening → kStopped
Play()                  →    kStopped → kPlaying
[播放中...]
Pause()                 →    kPlaying → kPaused
[暂停中...]
Play()                  →    kPaused → kPlaying
[播放中...]
Stop()                  →    kPlaying → kStopped
Close()                 →    kStopped → kIdle
```

**代码示例**:

```cpp
ZenPlayer player;

// 1. 打开文件
auto result = player.Open("video.mp4");
// 状态: kIdle → kOpening → kStopped

if (result.IsOk()) {
  // 2. 开始播放
  player.Play();
  // 状态: kStopped → kPlaying
  
  // 3. 暂停
  std::this_thread::sleep_for(std::chrono::seconds(5));
  player.Pause();
  // 状态: kPlaying → kPaused
  
  // 4. 恢复播放
  std::this_thread::sleep_for(std::chrono::seconds(2));
  player.Play();
  // 状态: kPaused → kPlaying
  
  // 5. 停止
  player.Stop();
  // 状态: kPlaying → kStopped
}

// 6. 关闭
player.Close();
// 状态: kStopped → kIdle
```

### 场景 2: Seek 跳转流程

```
用户操作                      状态转换
────────                      ────────
[播放中, kPlaying]
SeekAsync(30000)        →    kPlaying → kSeeking
[Seek 执行中...]
  - PreSeek()           →    暂停播放，清空队列
  - Demuxer::Seek()     →    文件跳转
  - ClearQueues()       →    清空所有缓冲
  - PostSeek()          →    恢复播放
[Seek 完成]             →    kSeeking → kPlaying
```

**代码示例**:

```cpp
// 播放过程中跳转
player.Play();
// 状态: kStopped → kPlaying

std::this_thread::sleep_for(std::chrono::seconds(5));

// 异步跳转到 30 秒
player.SeekAsync(30000);
// 状态: kPlaying → kSeeking

// Seek 在后台线程执行，不阻塞调用线程
// ...

// Seek 完成后自动恢复到 kPlaying
// 状态: kSeeking → kPlaying
```

**Seek 内部实现**:

```cpp
bool PlaybackController::ExecuteSeek(const SeekRequest& request) {
  // 1. 暂停播放
  if (audio_player_) {
    audio_player_->PreSeek();  // 清空音频缓冲
  }
  if (video_player_) {
    video_player_->PreSeek();  // 清空视频缓冲
  }
  
  // 2. 清空队列
  ClearAllQueues();
  
  // 3. 执行 Seek
  bool success = demuxer_->Seek(request.timestamp_ms, request.backward);
  
  if (success) {
    // 4. Seek 成功，恢复原状态
    if (audio_player_) {
      audio_player_->PostSeek(request.restore_state);
    }
    if (video_player_) {
      video_player_->PostSeek(request.restore_state);
    }
    
    // 5. 状态转换
    if (request.restore_state == PlayerState::kPlaying) {
      state_manager_->TransitionToPlaying();
    } else {
      state_manager_->TransitionToPaused();
    }
  } else {
    // Seek 失败
    state_manager_->TransitionToStopped();
  }
  
  seeking_.store(false);
  return success;
}
```

### 场景 3: 错误处理流程

```
用户操作                      状态转换
────────                      ────────
Open("invalid.mp4")     →    kIdle → kOpening → kError
Close()                 →    kError → kIdle

// 或者播放过程中出错
Play()                  →    kStopped → kPlaying
[解码错误]              →    kPlaying → kError
Close()                 →    kError → kIdle
```

**代码示例**:

```cpp
// Open 失败
auto result = player.Open("invalid.mp4");
if (!result.IsOk()) {
  // 状态: kIdle → kOpening → kError
  std::cerr << "Open failed: " << result.FullMessage() << std::endl;
  player.Close();  // 清理资源
  // 状态: kError → kIdle
}

// 播放过程中出错
player.Open("video.mp4");
player.Play();
// 状态: kStopped → kPlaying

// 假设解码器内部检测到错误
if (decode_error) {
  state_manager_->TransitionToError();
  // 状态: kPlaying → kError
}
```

---

## 💡 最佳实践

### 1. **工作线程使用状态管理器**

```cpp
void PlaybackController::VideoDecodeTask() {
  while (!state_manager_->ShouldStop()) {
    // ✅ 检查是否需要暂停
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }
    
    // 正常解码...
    auto packet = video_packet_queue_.Pop();
    video_decoder_->SendPacket(packet);
    auto frame = video_decoder_->ReceiveFrame();
    video_player_->PushFrame(frame);
  }
}
```

**要点**:
- 使用 `ShouldStop()` 检查是否应该退出
- 使用 `ShouldPause()` 检查是否应该暂停
- 使用 `WaitForResume()` 等待恢复

### 2. **状态转换前检查**

```cpp
Result<void> ZenPlayer::Play() {
  // ✅ 先检查当前状态
  if (!is_opened_ || !playback_controller_) {
    return Result<void>::Err(ErrorCode::kNotInitialized, 
                             "Player not opened");
  }
  
  // ✅ 如果已经在播放，直接返回
  if (state_manager_->IsPlaying()) {
    return Result<void>::Ok();
  }
  
  // ✅ 从暂停恢复
  if (state_manager_->IsPaused()) {
    playback_controller_->Resume();
    state_manager_->TransitionToPlaying();
    return Result<void>::Ok();
  }
  
  // ✅ 从停止开始播放
  state_manager_->TransitionToPlaying();
  return playback_controller_->Start();
}
```

### 3. **使用状态回调更新 UI**

```cpp
class MainWindow : public QMainWindow {
 private:
  void SetupStateCallback() {
    int callback_id = player_->RegisterStateChangeCallback(
        [this](PlayerState old_state, PlayerState new_state) {
          // ✅ 使用 Qt 的信号槽机制切换到 UI 线程
          QMetaObject::invokeMethod(
              this,
              [this, new_state]() {
                UpdateUIForState(new_state);
              },
              Qt::QueuedConnection);
        });
  }
  
  void UpdateUIForState(PlayerState state) {
    switch (state) {
      case PlayerState::kPlaying:
        playButton->setText("暂停");
        playButton->setEnabled(true);
        break;
      case PlayerState::kPaused:
        playButton->setText("播放");
        playButton->setEnabled(true);
        break;
      case PlayerState::kSeeking:
        statusLabel->setText("正在跳转...");
        seekSlider->setEnabled(false);
        break;
      case PlayerState::kError:
        statusLabel->setText("播放错误");
        playButton->setEnabled(false);
        break;
      // ...
    }
  }
};
```

### 4. **错误恢复**

```cpp
void MonitorPlayerState() {
  if (state_manager_->IsError()) {
    // ✅ 错误状态，尝试恢复
    MODULE_ERROR(LOG_MODULE_PLAYER, "Player in error state, attempting recovery");
    
    // 1. 关闭播放器
    player.Close();
    // 状态: kError → kIdle
    
    // 2. 重新打开
    auto result = player.Open(last_url_);
    if (result.IsOk()) {
      // 状态: kIdle → kOpening → kStopped
      
      // 3. 恢复播放
      player.Play();
      // 状态: kStopped → kPlaying
    }
  }
}
```

---

## ⚠️ 常见陷阱

### 陷阱 1: 忘记检查状态转换返回值

```cpp
// ❌ 错误：忽略返回值
state_manager_->TransitionToPlaying();
playback_controller_->Start();

// ✅ 正确：检查返回值
if (state_manager_->TransitionToPlaying()) {
  auto result = playback_controller_->Start();
  if (!result.IsOk()) {
    // 启动失败，回滚状态
    state_manager_->TransitionToStopped();
  }
}
```

### 陷阱 2: 状态不一致导致死锁

```cpp
// ❌ 错误：先转换状态，再暂停播放器
state_manager_->TransitionToPaused();  // 状态变为 Paused
audio_player_->Pause();  // 音频停止，但时钟还在更新

// ✅ 正确：先暂停播放器，再转换状态
audio_player_->Pause();  // 先停止音频输出
state_manager_->TransitionToPaused();  // 再改变状态
```

### 陷阱 3: 在回调中阻塞

```cpp
// ❌ 错误：在状态回调中做耗时操作
state_manager_->RegisterStateChangeCallback(
    [](PlayerState old_state, PlayerState new_state) {
      // 阻塞操作，会延迟状态通知
      std::this_thread::sleep_for(std::chrono::seconds(1));
    });

// ✅ 正确：异步处理
state_manager_->RegisterStateChangeCallback(
    [](PlayerState old_state, PlayerState new_state) {
      // 投递到工作线程或队列
      std::async(std::launch::async, [=]() {
        // 耗时操作
      });
    });
```

### 陷阱 4: 多线程竞态条件

```cpp
// ❌ 错误：先查询状态，后使用（TOCTOU 问题）
if (state_manager_->IsPlaying()) {
  // 此时其他线程可能已经改变了状态
  playback_controller_->Pause();  // 可能崩溃
}

// ✅ 正确：使用原子操作或先转换状态
if (state_manager_->TransitionToPaused()) {
  // 状态转换成功，安全操作
  playback_controller_->Pause();
}
```

---

## 📊 状态统计与调试

### 状态变更日志

```cpp
state_manager_->RegisterStateChangeCallback(
    [](PlayerState old_state, PlayerState new_state) {
      MODULE_INFO(LOG_MODULE_PLAYER, "State: {} -> {}",
                  PlayerStateManager::GetStateName(old_state),
                  PlayerStateManager::GetStateName(new_state));
    });
```

**输出示例**:

```
[INFO] State: Idle -> Opening
[INFO] State: Opening -> Stopped
[INFO] State: Stopped -> Playing
[INFO] State: Playing -> Paused
[INFO] State: Paused -> Playing
[INFO] State: Playing -> Seeking
[INFO] State: Seeking -> Playing
[INFO] State: Playing -> Stopped
[INFO] State: Stopped -> Idle
```

### 状态持续时间统计

```cpp
class StateMonitor {
 public:
  StateMonitor(PlayerStateManager* manager) {
    callback_id_ = manager->RegisterStateChangeCallback(
        [this](PlayerState old_state, PlayerState new_state) {
          OnStateChange(old_state, new_state);
        });
  }
  
 private:
  void OnStateChange(PlayerState old_state, PlayerState new_state) {
    auto now = std::chrono::steady_clock::now();
    
    // 记录旧状态的持续时间
    if (last_change_time_.time_since_epoch().count() > 0) {
      auto duration = now - last_change_time_;
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
      
      std::cout << "State " << PlayerStateManager::GetStateName(old_state)
                << " lasted " << ms << " ms" << std::endl;
    }
    
    last_change_time_ = now;
  }
  
  std::chrono::steady_clock::time_point last_change_time_;
  int callback_id_;
};
```

---

## 🔗 相关文档

- [整体架构设计](architecture_overview.md) - 理解状态管理器在系统中的位置
- [核心组件详解](core_components.md) - 各组件如何使用状态管理器
- [线程模型详解](threading_model.md) - 工作线程如何响应状态变化
- [Seek 专用线程](seek_thread.md) - Seek 流程中的状态转换

---

## 📝 总结

PlayerStateManager 是 ZenPlay 的**神经中枢**，负责协调所有组件的状态：

**核心价值**:
1. **单一状态源**: 避免状态不一致
2. **线程安全**: 原子操作保证多线程安全
3. **状态机设计**: 明确的状态转换规则
4. **观察者模式**: 解耦状态变更通知
5. **工作线程支持**: `ShouldStop()` / `WaitForResume()` 简化线程控制

**使用要点**:
- 总是检查状态转换返回值
- 使用 `WaitForResume()` 而非轮询
- 状态回调中不要阻塞
- 错误状态需要通过 `Close()` 恢复

通过良好的状态管理，ZenPlay 实现了清晰的控制流和可靠的并发控制。

---

**下一步阅读**: [音视频同步原理与实现](av_sync_design.md) - 深入了解 AVSyncController 的同步算法。
