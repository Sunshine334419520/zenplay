# PlayerStateManager 状态转换完整指南

## 📊 状态图总览

```
┌─────────┐
│  kIdle  │ ◄─────────────────────────────────┐
└────┬────┘                                    │
     │ Open()                                  │ Close()
     ▼                                         │
┌──────────┐                                   │
│ kOpening │                                   │
└────┬─────┘                                   │
     │ 文件打开成功                              │
     ▼                                         │
┌──────────┐                                   │
│ kStopped │ ◄─────────────┐                   │
└────┬─────┘                │                  │
     │ Play()          Stop()│                 │
     ▼                      │                  │
┌──────────┐                │                  │
│ kPlaying │ ───────────────┤                  │
└────┬─────┘                │                  │
     │ Pause()              │                  │
     ▼                      │                  │
┌──────────┐                │                  │
│ kPaused  │ ───────────────┘                  │
└──────────┘ Resume()→kPlaying                 │
     │                                         │
     │ (任何状态发生错误)                         │
     ▼                                         │
┌──────────┐                                   │
│  kError  │ ──────────────────────────────────┘
└──────────┘

┌────────────┐     ┌───────────┐
│ kBuffering │ ←─→ │ kSeeking  │
└────────────┘     └───────────┘
     ↕                   ↕
  kPlaying           kPlaying
```

---

## 🎯 8 种状态详解

### 1️⃣ kIdle（空闲状态）
**含义**：播放器初始化完成，但未打开任何文件

**何时进入**：
- 构造函数中初始化时
- `Close()` 关闭文件后

**可以转换到**：
- `kOpening` - 调用 `Open()` 时

**代码示例**：
```cpp
// ZenPlayer 构造函数
ZenPlayer::ZenPlayer() 
    : state_manager_(std::make_shared<PlayerStateManager>()) {
  // state_manager_ 默认初始化为 kIdle
}

// 关闭文件
void ZenPlayer::Close() {
  // ... 清理资源 ...
  state_manager_->TransitionToIdle();  // ✅ 回到空闲状态
}
```

---

### 2️⃣ kOpening（正在打开）
**含义**：正在打开媒体文件，解析格式，初始化解码器

**何时进入**：
- `Open()` 开始执行时

**可以转换到**：
- `kStopped` - 文件成功打开
- `kError` - 打开失败（文件不存在、格式不支持等）

**代码示例**：
```cpp
bool ZenPlayer::Open(const std::string& url) {
  // ✅ 1. 立即转换到 Opening 状态
  state_manager_->TransitionToOpening();
  
  // 2. 执行打开操作
  if (!demuxer_->Open(url)) {
    // ❌ 打开失败，转到 Error
    state_manager_->TransitionToError();
    return false;
  }
  
  if (!video_decoder_->Open(video_stream->codecpar)) {
    state_manager_->TransitionToError();
    return false;
  }
  
  // ✅ 3. 成功打开，转到 Stopped（就绪但未播放）
  state_manager_->TransitionToStopped();
  return true;
}
```

**⚠️ 当前代码问题**：
你的代码中 `Open()` **没有调用** `TransitionToOpening()`，这是一个遗漏！

---

### 3️⃣ kStopped（已停止）
**含义**：文件已打开，解码器就绪，但未开始播放

**何时进入**：
- `Open()` 成功完成后
- `Stop()` 停止播放后
- 从 `kPaused` 调用 `Stop()`

**可以转换到**：
- `kPlaying` - 调用 `Play()`
- `kSeeking` - 调用 `Seek()`（停止时也可跳转）
- `kIdle` - 调用 `Close()`

**代码示例**：
```cpp
bool ZenPlayer::Stop() {
  if (state_manager_->IsStopped()) {
    return true;  // 已经是停止状态
  }
  
  playback_controller_->Stop();  // 停止所有线程
  state_manager_->TransitionToStopped();  // ✅ 转到停止状态
  return true;
}
```

---

### 4️⃣ kPlaying（正在播放）
**含义**：正在播放视频/音频，解码线程、渲染线程都在运行

**何时进入**：
- 从 `kStopped` 调用 `Play()`
- 从 `kPaused` 调用 `Resume()` 或 `Play()`
- 从 `kBuffering` 缓冲完成后自动恢复
- 从 `kSeeking` 跳转完成后自动恢复

**可以转换到**：
- `kPaused` - 调用 `Pause()`
- `kStopped` - 调用 `Stop()`
- `kSeeking` - 调用 `Seek()`
- `kBuffering` - 队列为空，需要缓冲
- `kError` - 解码错误、渲染错误

**代码示例**：
```cpp
bool ZenPlayer::Play() {
  // 从暂停恢复
  if (state_manager_->IsPaused()) {
    playback_controller_->Resume();
    state_manager_->TransitionToPlaying();  // ✅
    return true;
  }
  
  // 从停止开始播放
  if (playback_controller_->Start()) {
    state_manager_->TransitionToPlaying();  // ✅
    return true;
  }
  
  return false;
}
```

---

### 5️⃣ kPaused（已暂停）
**含义**：播放暂停，解码线程暂停，渲染线程等待，保持当前帧显示

**何时进入**：
- 从 `kPlaying` 调用 `Pause()`

**可以转换到**：
- `kPlaying` - 调用 `Resume()` 或 `Play()`
- `kStopped` - 调用 `Stop()`
- `kSeeking` - 调用 `Seek()`

**代码示例**：
```cpp
bool ZenPlayer::Pause() {
  if (!state_manager_->IsPlaying()) {
    return false;  // 只能在播放时暂停
  }
  
  playback_controller_->Pause();  // 通知所有线程暂停
  state_manager_->TransitionToPaused();  // ✅
  return true;
}
```

---

### 6️⃣ kBuffering（缓冲中）
**含义**：播放过程中数据不足，正在缓冲更多数据

**何时进入**：
- 🔥 **PlaybackController** 检测到视频/音频队列为空
- 网络播放时数据下载不及时
- 磁盘 IO 慢导致读取延迟

**可以转换到**：
- `kPlaying` - 缓冲足够数据后自动恢复
- `kStopped` - 用户手动停止
- `kError` - 缓冲超时或读取失败

**代码示例**（在 PlaybackController 中实现）：
```cpp
void PlaybackController::VideoDecodeTask() {
  while (!state_manager_->ShouldStop()) {
    // 检查视频队列大小
    if (video_player_->GetQueueSize() < MIN_BUFFER_THRESHOLD) {
      // ⚠️ 队列不足，进入缓冲状态
      if (state_manager_->GetState() == PlayerState::kPlaying) {
        state_manager_->TransitionToBuffering();
        MODULE_WARN(LOG_MODULE_PLAYER, "Video buffering: queue size = {}", 
                    video_player_->GetQueueSize());
      }
    } else if (video_player_->GetQueueSize() > RESUME_BUFFER_THRESHOLD) {
      // ✅ 缓冲足够，恢复播放
      if (state_manager_->GetState() == PlayerState::kBuffering) {
        state_manager_->TransitionToPlaying();
        MODULE_INFO(LOG_MODULE_PLAYER, "Buffering complete, resuming");
      }
    }
    
    // ... 解码逻辑 ...
  }
}
```

**⚠️ 当前代码问题**：
你的代码中 **没有实现 Buffering 检测**！需要在 `PlaybackController` 的解码线程中添加队列监控。

---

### 7️⃣ kSeeking（跳转中）
**含义**：正在执行时间轴跳转，清空缓冲区，定位到新位置

**何时进入**：
- 调用 `Seek()` 时

**可以转换到**：
- `kPlaying` - 跳转完成，恢复播放
- `kPaused` - 跳转完成，保持暂停
- `kStopped` - 用户手动停止
- `kBuffering` - 跳转后需要缓冲
- `kError` - 跳转失败（时间戳无效）

**代码示例**（需要实现）：
```cpp
bool ZenPlayer::Seek(int64_t timestamp) {
  if (!is_opened_ || !playback_controller_) {
    return false;
  }
  
  // 1. 保存当前状态
  auto previous_state = state_manager_->GetState();
  
  // 2. 转换到 Seeking 状态
  state_manager_->TransitionToSeeking();
  
  // 3. 执行跳转
  bool success = playback_controller_->Seek(timestamp);
  
  if (success) {
    // 4. 恢复到之前的状态
    if (previous_state == PlayerState::kPlaying) {
      state_manager_->TransitionToPlaying();
    } else if (previous_state == PlayerState::kPaused) {
      state_manager_->TransitionToPaused();
    } else {
      state_manager_->TransitionToStopped();
    }
  } else {
    // 跳转失败
    state_manager_->TransitionToError();
  }
  
  return success;
}
```

**⚠️ 当前代码问题**：
你的 `Seek()` 实现**过于简单**，没有状态管理！

---

### 8️⃣ kError（错误状态）
**含义**：发生不可恢复的错误

**何时进入**：
- `Open()` 失败
- 解码器初始化失败
- 关键资源分配失败
- 解码过程中持续错误
- 渲染失败

**可以转换到**：
- `kIdle` - 调用 `Close()` 清理错误
- `kStopped` - 尝试恢复（如果可能）

**代码示例**：
```cpp
void PlaybackController::VideoDecodeTask() {
  int consecutive_errors = 0;
  const int MAX_ERRORS = 10;
  
  while (!state_manager_->ShouldStop()) {
    bool decode_success = video_decoder_->Decode(packet, &frames);
    
    if (!decode_success) {
      consecutive_errors++;
      if (consecutive_errors > MAX_ERRORS) {
        // 连续错误过多，进入错误状态
        state_manager_->TransitionToError();
        MODULE_ERROR(LOG_MODULE_PLAYER, "Too many decode errors, stopping");
        break;
      }
    } else {
      consecutive_errors = 0;  // 重置计数器
    }
  }
}
```

---

## 🔄 状态转换矩阵

| 从 ↓ / 到 → | Idle | Opening | Stopped | Playing | Paused | Seeking | Buffering | Error |
|------------|------|---------|---------|---------|--------|---------|-----------|-------|
| **Idle**       | -    | ✅      | ❌      | ❌      | ❌     | ❌      | ❌        | ❌    |
| **Opening**    | ❌   | -       | ✅      | ❌      | ❌     | ❌      | ❌        | ✅    |
| **Stopped**    | ✅   | ❌      | -       | ✅      | ❌     | ✅      | ❌        | ❌    |
| **Playing**    | ❌   | ❌      | ✅      | -       | ✅     | ✅      | ✅        | ✅    |
| **Paused**     | ❌   | ❌      | ✅      | ✅      | -      | ✅      | ❌        | ❌    |
| **Seeking**    | ❌   | ❌      | ✅      | ✅      | ✅     | -       | ✅        | ✅    |
| **Buffering**  | ❌   | ❌      | ✅      | ✅      | ❌     | ❌      | -         | ✅    |
| **Error**      | ✅   | ❌      | ✅      | ❌      | ❌     | ❌      | ❌        | -     |

---

## 📋 当前代码需要改进的地方

### ❌ 问题 1：Open() 缺少 kOpening 状态

**当前代码**：
```cpp
bool ZenPlayer::Open(const std::string& url) {
  // 直接开始打开，没有状态转换
  if (!demuxer_->Open(url)) {
    return false;  // ❌ 失败时也没转到 kError
  }
  // ...
  state_manager_->TransitionToStopped();  // 只在最后转换
}
```

**改进建议**：
```cpp
bool ZenPlayer::Open(const std::string& url) {
  // ✅ 1. 立即转换到 Opening
  state_manager_->TransitionToOpening();
  
  if (!demuxer_->Open(url)) {
    // ✅ 2. 失败转到 Error
    state_manager_->TransitionToError();
    return false;
  }
  
  // ... 其他初始化 ...
  
  // ✅ 3. 成功转到 Stopped
  state_manager_->TransitionToStopped();
  return true;
}
```

---

### ❌ 问题 2：缺少 Buffering 检测

**需要添加**：
```cpp
// 在 PlaybackController::VideoDecodeTask() 中
void PlaybackController::VideoDecodeTask() {
  const int MIN_FRAMES = 5;   // 最小缓冲帧数
  const int RESUME_FRAMES = 10; // 恢复播放帧数
  
  while (!state_manager_->ShouldStop()) {
    int queue_size = video_player_->GetQueueSize();
    auto current_state = state_manager_->GetState();
    
    // ✅ 检测是否需要缓冲
    if (current_state == PlayerState::kPlaying && queue_size < MIN_FRAMES) {
      state_manager_->TransitionToBuffering();
      MODULE_WARN(LOG_MODULE_PLAYER, "Video buffering started");
    }
    
    // ✅ 检测缓冲是否完成
    if (current_state == PlayerState::kBuffering && queue_size >= RESUME_FRAMES) {
      state_manager_->TransitionToPlaying();
      MODULE_INFO(LOG_MODULE_PLAYER, "Buffering complete");
    }
    
    // ... 解码逻辑 ...
  }
}
```

---

### ❌ 问题 3：Seek() 没有状态管理

**当前代码**：
```cpp
bool ZenPlayer::Seek(int64_t timestamp) {
  return playback_controller_->Seek(timestamp);  // ❌ 太简单
}
```

**改进建议**：
```cpp
bool ZenPlayer::Seek(int64_t timestamp) {
  if (!is_opened_) {
    return false;
  }
  
  // 保存原状态
  auto prev_state = state_manager_->GetState();
  bool was_playing = (prev_state == PlayerState::kPlaying);
  
  // ✅ 转到 Seeking
  state_manager_->TransitionToSeeking();
  
  // 执行跳转
  bool success = playback_controller_->Seek(timestamp);
  
  if (success) {
    // ✅ 恢复原状态或转到 Buffering
    if (was_playing) {
      // 可能需要先缓冲
      state_manager_->TransitionToBuffering();
    } else {
      state_manager_->TransitionToStopped();
    }
  } else {
    state_manager_->TransitionToError();
  }
  
  return success;
}
```

---

## 🎬 完整播放流程示例

### 场景：用户打开文件并播放

```
1. 用户点击"打开文件"
   └─> Open("video.mp4")
       ├─> TransitionToOpening()    // 开始打开
       ├─> demuxer_->Open()          // 解析文件
       ├─> video_decoder_->Open()    // 初始化解码器
       └─> TransitionToStopped()     // 就绪，等待播放

2. 用户点击"播放"
   └─> Play()
       ├─> playback_controller_->Start()
       └─> TransitionToPlaying()     // 开始播放

3. 播放过程中队列不足
   └─> PlaybackController 检测
       └─> TransitionToBuffering()   // 自动缓冲

4. 缓冲完成
   └─> PlaybackController 检测
       └─> TransitionToPlaying()     // 恢复播放

5. 用户点击"暂停"
   └─> Pause()
       ├─> playback_controller_->Pause()
       └─> TransitionToPaused()      // 暂停

6. 用户拖动进度条
   └─> Seek(timestamp)
       ├─> TransitionToSeeking()     // 开始跳转
       ├─> playback_controller_->Seek()
       └─> TransitionToBuffering()   // 跳转后缓冲

7. 用户点击"停止"
   └─> Stop()
       ├─> playback_controller_->Stop()
       └─> TransitionToStopped()     // 停止

8. 用户关闭文件
   └─> Close()
       ├─> Stop()                    // 先停止
       ├─> 清理资源
       └─> TransitionToIdle()        // 回到空闲
```

---

## 📝 总结：你需要在哪里添加状态转换

| 位置 | 状态转换 | 优先级 |
|-----|---------|--------|
| `ZenPlayer::Open()` 开始时 | `TransitionToOpening()` | 🔴 高 |
| `ZenPlayer::Open()` 失败时 | `TransitionToError()` | 🔴 高 |
| `PlaybackController::VideoDecodeTask()` | 检测 `Buffering` ↔ `Playing` | 🔴 高 |
| `PlaybackController::AudioDecodeTask()` | 检测 `Buffering` ↔ `Playing` | 🔴 高 |
| `ZenPlayer::Seek()` | `Seeking` → 原状态 | 🟡 中 |
| 解码错误处理 | `TransitionToError()` | 🟢 低 |

---

希望这份指南能帮助你理解每个状态的作用和转换时机！🎯
