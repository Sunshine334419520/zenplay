# 同步模式选择指南

## SyncMode 概述

`AVSyncController` 提供三种同步模式：

```cpp
enum class SyncMode {
  AUDIO_MASTER,    // 以音频为主时钟
  VIDEO_MASTER,    // 以视频为主时钟
  EXTERNAL_MASTER  // 以外部时钟（系统时钟）为主
};
```

---

## 使用场景详解

### 1. AUDIO_MASTER（音频主时钟）- 默认推荐

**适用场景**：
- ✅ **有音频 + 有视频**（最常见）
- ✅ **只有音频**（音乐播放、播客等）

**原理**：
- 音频时钟作为基准，视频同步到音频
- 音频播放由硬件驱动，时钟最稳定
- 音频不能有卡顿（人耳敏感），所以音频连续播放，视频适应音频

**为什么稳定**：
```
音频硬件 → 音频回调（固定频率）→ UpdateAudioClock
                                    ↓
视频渲染 → CalculateVideoDelay → 根据音频时钟调整显示时间
```

**优势**：
- 🎵 音频播放流畅，无卡顿
- 🎬 视频通过丢帧/重复帧适应音频
- ⏱️ 时钟稳定，由音频硬件驱动

**只有音频时的行为**：
- 音频正常播放
- `GetMasterClock()` 返回音频时钟
- 虽然没有视频需要同步，但时钟系统仍然正常工作

---

### 2. VIDEO_MASTER（视频主时钟）- 特殊场景

**适用场景**：
- ⚠️ **有视频 + 有音频，但需要视频优先**（罕见）
  - 例如：视频演示/教学，画面更重要
  - 例如：逐帧分析工具

**原理**：
- 视频时钟作为基准，音频同步到视频
- 视频按固定帧率播放，音频适应视频

**问题**：
- ❌ 音频可能产生卡顿（音频重采样/跳跃）
- ❌ 人耳对音频卡顿非常敏感，体验差
- ⚠️ 只在特殊需求下使用

**不推荐用于纯视频**：
- 如果只有视频，应该用 `EXTERNAL_MASTER`，而不是 `VIDEO_MASTER`
- `VIDEO_MASTER` 的意义是"让音频同步到视频"，没有音频时没有意义

---

### 3. EXTERNAL_MASTER（外部时钟/系统时钟）- 简单场景

**适用场景**：
- ✅ **只有视频**（无音频）
- ✅ 测试和调试
- ✅ 简单播放器（不需要复杂同步）

**原理**：
- 使用系统时钟（`play_start_time_`）作为基准
- 计算公式：`elapsed_time = now - play_start_time_`
- 视频 PTS 与 elapsed_time 比较，决定显示时间

**优势**：
- 📐 简单直观，易于理解
- 🔧 方便调试，时钟行为可预测
- 💡 无音频时的最佳选择

**只有视频时的行为**：
```cpp
// GetMasterClock 返回播放经过的时间
double GetMasterClock(now) {
  return (now - play_start_time_).count();  // 自动排除暂停时间
}

// CalculateVideoDelay 计算视频延迟
double delay = video_pts - GetMasterClock(now);
// delay > 0: 视频超前，延迟显示
// delay < 0: 视频落后，立即显示或丢帧
```

---

## 推荐的选择策略

### 决策树

```
媒体文件
  ├─ 有音频？
  │   ├─ 是 → 有视频？
  │   │   ├─ 是 → AUDIO_MASTER ✅ (音视频同步)
  │   │   └─ 否 → AUDIO_MASTER ✅ (纯音频)
  │   │
  │   └─ 否 → 有视频？
  │       ├─ 是 → EXTERNAL_MASTER ✅ (纯视频)
  │       └─ 否 → 错误（无音频也无视频）
  │
  └─ 特殊需求（视频优先）？
      └─ 是 → VIDEO_MASTER ⚠️ (音频可能卡顿)
```

### 简化规则

| 场景 | 推荐模式 | 理由 |
|------|---------|------|
| 音频 + 视频 | `AUDIO_MASTER` | 音频稳定，视频适应 |
| 只有音频 | `AUDIO_MASTER` | 音频硬件驱动，时钟稳定 |
| 只有视频 | `EXTERNAL_MASTER` | 系统时钟简单可靠 |
| 特殊需求（视频优先）| `VIDEO_MASTER` | 视频为主，音频适应（体验差） |

---

## 当前代码分析

### 当前实现（不完整）

```cpp
// playback_controller.cpp
if (!audio_decoder_ || !audio_decoder_->opened()) {
  // 仅视频播放：使用外部时钟
  av_sync_controller_->SetSyncMode(
      AVSyncController::SyncMode::EXTERNAL_MASTER);
} else {
  // 音视频播放：使用音频主时钟
  av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
}
```

**问题**：
- ✅ 有音频 + 有视频 → `AUDIO_MASTER` ✅ 正确
- ✅ 无音频 + 有视频 → `EXTERNAL_MASTER` ✅ 正确
- ❌ **只有音频（无视频）** → `AUDIO_MASTER` ✅ 正确，但缺少日志说明
- ⚠️ 没有考虑 `VIDEO_MASTER` 的使用场景（可以保持不支持）

---

## 优化建议

### 方案 1：完善当前逻辑（推荐）

```cpp
// 根据解码器状态智能选择同步模式
bool has_audio = audio_decoder_ && audio_decoder_->opened();
bool has_video = video_decoder_ && video_decoder_->opened();

if (has_audio && has_video) {
  // 音视频都有：使用音频主时钟（标准播放）
  av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
  MODULE_INFO(LOG_MODULE_PLAYER,
              "Audio + Video detected, using AUDIO_MASTER sync mode");
              
} else if (has_audio && !has_video) {
  // 只有音频：使用音频主时钟（音乐播放、播客等）
  av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
  MODULE_INFO(LOG_MODULE_PLAYER,
              "Audio only detected, using AUDIO_MASTER sync mode");
              
} else if (!has_audio && has_video) {
  // 只有视频：使用外部时钟（系统时钟）
  av_sync_controller_->SetSyncMode(
      AVSyncController::SyncMode::EXTERNAL_MASTER);
  MODULE_INFO(LOG_MODULE_PLAYER,
              "Video only detected, using EXTERNAL_MASTER sync mode");
              
} else {
  // 既无音频也无视频：错误情况
  MODULE_ERROR(LOG_MODULE_PLAYER,
               "No audio and no video, invalid media file");
  // 使用外部时钟作为后备
  av_sync_controller_->SetSyncMode(
      AVSyncController::SyncMode::EXTERNAL_MASTER);
}
```

---

### 方案 2：支持用户配置（高级）

如果需要支持用户手动选择同步模式（例如在设置中）：

```cpp
enum class SyncModePreference {
  AUTO,          // 自动选择（推荐）
  FORCE_AUDIO,   // 强制音频主时钟
  FORCE_VIDEO,   // 强制视频主时钟
  FORCE_EXTERNAL // 强制外部时钟
};

void PlaybackController::ApplySyncMode(SyncModePreference preference) {
  bool has_audio = audio_decoder_ && audio_decoder_->opened();
  bool has_video = video_decoder_ && video_decoder_->opened();
  
  AVSyncController::SyncMode mode;
  
  switch (preference) {
    case SyncModePreference::AUTO:
      // 自动选择（推荐）
      if (has_audio) {
        mode = AVSyncController::SyncMode::AUDIO_MASTER;
      } else {
        mode = AVSyncController::SyncMode::EXTERNAL_MASTER;
      }
      break;
      
    case SyncModePreference::FORCE_AUDIO:
      mode = AVSyncController::SyncMode::AUDIO_MASTER;
      if (!has_audio) {
        MODULE_WARN(LOG_MODULE_PLAYER, 
                    "FORCE_AUDIO selected but no audio available");
      }
      break;
      
    case SyncModePreference::FORCE_VIDEO:
      mode = AVSyncController::SyncMode::VIDEO_MASTER;
      if (!has_video) {
        MODULE_WARN(LOG_MODULE_PLAYER, 
                    "FORCE_VIDEO selected but no video available");
      }
      break;
      
    case SyncModePreference::FORCE_EXTERNAL:
      mode = AVSyncController::SyncMode::EXTERNAL_MASTER;
      break;
  }
  
  av_sync_controller_->SetSyncMode(mode);
}
```

---

## VIDEO_MASTER 的实现问题

### 当前实现分析

`VIDEO_MASTER` 模式在当前代码中**理论上可用**，但有以下问题：

1. **音频同步到视频的逻辑缺失**
   ```cpp
   // CalculateAudioAdjustment 有基本实现
   double CalculateAudioAdjustment(...) {
     if (sync_mode_ != SyncMode::VIDEO_MASTER) {
       return 0.0;  // 只在 VIDEO_MASTER 时调整音频
     }
     // ... 计算音频调整量
   }
   ```
   
   但 `AudioPlayer` 需要使用这个调整量来实现音频重采样/跳跃，**当前可能未实现**。

2. **视频帧率不稳定**
   - 音频硬件回调是稳定的（例如每 10ms）
   - 视频帧率可能不稳定（解码、渲染延迟）
   - 以不稳定的时钟同步稳定的时钟 → 体验差

3. **推荐做法**
   - 不主动支持 `VIDEO_MASTER`（保留代码但不暴露给用户）
   - 如果未来需要，再完善音频重采样逻辑

---

## 实际使用示例

### 示例 1：标准视频播放（有音频 + 视频）

```
用户打开 movie.mp4（音频：AAC，视频：H.264）

PlaybackController::PlaybackController()
  ├─ audio_decoder_->opened() = true
  ├─ video_decoder_->opened() = true
  └─ SetSyncMode(AUDIO_MASTER) ✅
  
播放过程：
  ├─ 音频硬件回调 → UpdateAudioClock(pts=1000ms)
  ├─ 视频解码完成 → CalculateVideoDelay(pts=1020ms)
  │   └─ master_clock = 1000ms (音频时钟)
  │   └─ delay = 1020 - 1000 = 20ms
  │   └─ 等待 20ms 后显示 ✅
  └─ 音画同步！
```

---

### 示例 2：纯音频播放（音乐、播客）

```
用户打开 music.mp3（音频：MP3，无视频）

PlaybackController::PlaybackController()
  ├─ audio_decoder_->opened() = true
  ├─ video_decoder_ = nullptr (或未打开)
  └─ SetSyncMode(AUDIO_MASTER) ✅
  
播放过程：
  ├─ 音频硬件回调 → UpdateAudioClock(pts=1000ms)
  ├─ GetMasterClock() → 返回 1000ms
  └─ 虽然没有视频，但时钟系统仍然正常工作 ✅
```

---

### 示例 3：纯视频播放（GIF、静默视频）

```
用户打开 silent_video.mp4（无音频，视频：H.264）

PlaybackController::PlaybackController()
  ├─ audio_decoder_ = nullptr (或未打开)
  ├─ video_decoder_->opened() = true
  └─ SetSyncMode(EXTERNAL_MASTER) ✅
  
播放过程：
  ├─ play_start_time_ = T0
  ├─ 视频解码完成 → CalculateVideoDelay(pts=1000ms)
  │   └─ master_clock = (now - T0) = 500ms
  │   └─ delay = 1000 - 500 = 500ms
  │   └─ 等待 500ms 后显示 ✅
  └─ 视频按固定帧率播放！
```

---

## 总结

### 核心原则

1. **有音频 → 用音频时钟**
   - 原因：音频硬件稳定，音频卡顿体验差
   - 场景：音视频同步、纯音频播放

2. **无音频 → 用系统时钟**
   - 原因：简单可靠，无需复杂同步
   - 场景：纯视频播放、测试调试

3. **VIDEO_MASTER 慎用**
   - 原因：音频适应视频，可能产生卡顿
   - 场景：仅在特殊需求时使用（视频逐帧分析等）

### 推荐实现

```cpp
// 简单且正确的选择策略
if (has_audio) {
  SetSyncMode(AUDIO_MASTER);  // 有音频就用音频时钟
} else {
  SetSyncMode(EXTERNAL_MASTER);  // 无音频就用系统时钟
}
```

这个策略覆盖了 99% 的使用场景，简单、可靠、易维护！

---

**文档版本**: v1.0  
**创建时间**: 2025-10-13  
**作者**: GitHub Copilot
