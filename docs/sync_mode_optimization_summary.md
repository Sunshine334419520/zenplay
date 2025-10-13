# SyncMode 优化总结

## 问题

用户询问：
- 音频 + 视频 → 使用 `AUDIO_MASTER` ✅
- **只有视频 → 应该使用什么？**
- **只有音频 → 应该使用什么？**

## 答案

### 完整的选择策略

| 场景 | 推荐模式 | 原因 |
|------|---------|------|
| 音频 + 视频 | `AUDIO_MASTER` | 音频稳定，视频适应音频 |
| **只有音频** | `AUDIO_MASTER` | 音频硬件驱动，时钟最稳定 |
| **只有视频** | `EXTERNAL_MASTER` | 系统时钟简单可靠 |
| 特殊需求 | `VIDEO_MASTER` | 视频优先（音频可能卡顿） |

### 核心原则

```
有音频 → 用音频时钟 (AUDIO_MASTER)
无音频 → 用系统时钟 (EXTERNAL_MASTER)
```

---

## 修改内容

### 1. 完善同步模式选择逻辑

#### 文件：`src/player/playback_controller.cpp`

**修改前**（不完整）：
```cpp
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
- 只检查了音频，没有区分"只有音频"和"音频+视频"
- 日志信息不够详细

**修改后**（完整且清晰）：
```cpp
// 根据音视频流的存在情况智能选择同步模式
bool has_audio = audio_decoder_ && audio_decoder_->opened();
bool has_video = video_decoder_ && video_decoder_->opened();

if (has_audio && has_video) {
  // 场景 1：音视频都有 → 使用音频主时钟（标准播放）
  av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
  MODULE_INFO(LOG_MODULE_PLAYER,
              "Audio + Video detected, using AUDIO_MASTER sync mode");

} else if (has_audio && !has_video) {
  // 场景 2：只有音频 → 使用音频主时钟（音乐播放、播客等）
  av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
  MODULE_INFO(LOG_MODULE_PLAYER,
              "Audio only detected, using AUDIO_MASTER sync mode");

} else if (!has_audio && has_video) {
  // 场景 3：只有视频 → 使用外部时钟/系统时钟
  av_sync_controller_->SetSyncMode(
      AVSyncController::SyncMode::EXTERNAL_MASTER);
  MODULE_INFO(LOG_MODULE_PLAYER,
              "Video only detected, using EXTERNAL_MASTER sync mode");

} else {
  // 场景 4：既无音频也无视频 → 错误情况
  MODULE_ERROR(LOG_MODULE_PLAYER,
               "No audio and no video streams available, invalid media file");
  av_sync_controller_->SetSyncMode(
      AVSyncController::SyncMode::EXTERNAL_MASTER);
}
```

**改进**：
- ✅ 明确区分 4 种场景
- ✅ 详细的日志输出，便于调试
- ✅ 处理了无效媒体文件的情况
- ✅ 每个分支都有清晰的注释说明

---

### 2. 增强 SyncMode 枚举文档

#### 文件：`src/player/sync/av_sync_controller.h`

**修改前**（简单注释）：
```cpp
enum class SyncMode {
  AUDIO_MASTER,    // 以音频为主时钟（推荐）
  VIDEO_MASTER,    // 以视频为主时钟
  EXTERNAL_MASTER  // 外部时钟（如系统时钟）
};
```

**修改后**（详细文档）：
```cpp
/**
 * @brief 同步模式
 *
 * 定义音视频同步的主时钟来源。主时钟决定播放进度，其他流同步到主时钟。
 *
 * 选择原则：
 * - 有音频 → 用 AUDIO_MASTER（音频硬件稳定，音频卡顿体验差）
 * - 无音频 → 用 EXTERNAL_MASTER（系统时钟简单可靠）
 * - VIDEO_MASTER 仅用于特殊场景（视频逐帧分析等，音频可能卡顿）
 */
enum class SyncMode {
  /**
   * 音频主时钟模式（推荐）
   *
   * 适用场景：
   * - 音频 + 视频（标准播放）
   * - 只有音频（音乐、播客等）
   *
   * 原理：音频时钟由硬件驱动，最稳定
   * 优势：音频流畅，音画同步准确
   */
  AUDIO_MASTER,

  /**
   * 视频主时钟模式（特殊场景）
   *
   * 适用场景：视频演示/教学、逐帧分析工具
   * 缺点：音频可能卡顿，用户体验较差
   * 注意：当前实现可能未完全支持音频重采样
   */
  VIDEO_MASTER,

  /**
   * 外部时钟/系统时钟模式
   *
   * 适用场景：
   * - 只有视频（GIF、静默视频等）
   * - 测试和调试
   *
   * 优势：简单直观，易于调试
   */
  EXTERNAL_MASTER
};
```

**改进**：
- ✅ 每个模式都有详细的使用说明
- ✅ 明确标注适用场景
- ✅ 说明优缺点和注意事项
- ✅ 帮助开发者做出正确选择

---

### 3. 创建详细指南文档

#### 文件：`docs/sync_mode_selection_guide.md`

包含内容：
- 📖 SyncMode 概述和原理说明
- 🎯 各种场景的详细分析
- 📊 决策树和选择策略
- 💡 实际使用示例
- ⚠️ VIDEO_MASTER 的实现问题分析
- ✅ 推荐做法和最佳实践

---

## 各场景详细说明

### 场景 1：音频 + 视频（标准播放）

**示例**：播放 movie.mp4（AAC 音频 + H.264 视频）

**选择**：`AUDIO_MASTER` ✅

**原理**：
```
音频硬件回调（固定频率）
  ↓
UpdateAudioClock(audio_pts)
  ↓
音频时钟更新
  ↓
CalculateVideoDelay(video_pts)
  ├─ master_clock = 音频时钟
  └─ delay = video_pts - master_clock
  ↓
视频根据 delay 调整显示时间
  ├─ delay > 0 → 延迟显示
  ├─ delay < 0 → 丢帧或立即显示
  └─ 音画同步！
```

**为什么用音频时钟**：
- 音频硬件回调稳定（例如每 10ms）
- 音频不能卡顿（人耳敏感）
- 视频可以通过丢帧/重复帧适应音频

---

### 场景 2：只有音频（音乐、播客）

**示例**：播放 music.mp3（MP3 音频，无视频）

**选择**：`AUDIO_MASTER` ✅

**原理**：
```
音频硬件回调
  ↓
UpdateAudioClock(audio_pts)
  ↓
GetMasterClock() → 返回音频时钟
  ↓
虽然没有视频需要同步，但时钟系统仍然正常工作
```

**为什么用音频时钟**：
- 音频时钟由硬件驱动，最稳定
- 即使没有视频，时钟系统也能正常工作
- 为将来可能添加的可视化效果（频谱、歌词）提供时钟基准

---

### 场景 3：只有视频（GIF、静默视频）

**示例**：播放 silent_video.mp4（无音频，H.264 视频）

**选择**：`EXTERNAL_MASTER` ✅

**原理**：
```
play_start_time_ 记录播放开始时间
  ↓
GetMasterClock(now)
  └─ return (now - play_start_time_)  // 播放经过的时间
  ↓
CalculateVideoDelay(video_pts)
  ├─ master_clock = 播放经过的时间
  └─ delay = video_pts - master_clock
  ↓
视频按固定帧率播放
```

**为什么用系统时钟**：
- 没有音频时钟可用
- 系统时钟简单可靠
- 计算公式直观，易于调试
- 暂停/恢复时 `play_start_time_` 会自动调整

**为什么不用 VIDEO_MASTER**：
- `VIDEO_MASTER` 的意义是"让音频同步到视频"
- 没有音频时，`VIDEO_MASTER` 没有意义
- `EXTERNAL_MASTER` 更简单且同样有效

---

### 场景 4：特殊需求（视频优先）

**示例**：视频教学软件，需要逐帧查看

**选择**：`VIDEO_MASTER` ⚠️（慎用）

**原理**：
```
视频解码 → UpdateVideoClock(video_pts)
  ↓
GetMasterClock() → 返回视频时钟
  ↓
CalculateAudioAdjustment(audio_pts)
  ├─ master_clock = 视频时钟
  └─ adjustment = audio_pts - master_clock
  ↓
音频播放器根据 adjustment 重采样/跳跃
  └─ 音频可能产生卡顿！
```

**缺点**：
- 视频帧率不稳定（解码延迟、渲染延迟）
- 音频需要频繁调整（重采样或跳跃）
- 人耳对音频卡顿非常敏感
- 用户体验差

**当前实现问题**：
- `CalculateAudioAdjustment()` 有基本实现
- 但 `AudioPlayer` 可能未实现音频重采样/跳跃
- 需要完善才能真正支持 `VIDEO_MASTER`

**推荐**：
- 大多数情况下不要使用 `VIDEO_MASTER`
- 如果真的需要，请先完善音频调整逻辑

---

## 时钟推算详解

### AUDIO_MASTER 模式

```cpp
// 音频回调更新时钟
UpdateAudioClock(audio_pts=1000ms, system_time=T0);
  └─ audio_clock_.pts_ms = 1000ms
  └─ audio_clock_.system_time = T0

// 稍后查询主时钟（例如 500ms 后）
GetMasterClock(current_time=T0+500ms)
  └─ return audio_clock_.GetCurrentTime(T0+500ms)
      └─ elapsed = (T0+500ms) - T0 = 500ms
      └─ return 1000ms + 500ms + drift = 1500ms

// 视频同步计算
CalculateVideoDelay(video_pts=1520ms, current_time=T0+500ms)
  └─ normalized_video_pts = 1520ms
  └─ master_clock = GetMasterClock() = 1500ms
  └─ delay = 1520 - 1500 = 20ms
  └─ 等待 20ms 后显示视频帧 ✅
```

---

### EXTERNAL_MASTER 模式（无音频）

```cpp
// 播放开始
play_start_time_ = T0

// 查询主时钟（播放 1 秒后）
GetMasterClock(current_time=T0+1000ms)
  └─ elapsed = (T0+1000ms) - T0 = 1000ms
  └─ return 1000ms

// 视频同步计算
CalculateVideoDelay(video_pts=1020ms, current_time=T0+1000ms)
  └─ normalized_video_pts = 1020ms
  └─ master_clock = GetMasterClock() = 1000ms
  └─ delay = 1020 - 1000 = 20ms
  └─ 等待 20ms 后显示视频帧 ✅
```

---

### 暂停/恢复的影响

无论哪种模式，暂停/恢复都能正确工作：

```cpp
// 暂停前：播放了 1 秒
GetMasterClock() = 1000ms

// 暂停 5 秒
Pause() → 记录 pause_start_time_

// 恢复
Resume()
  └─ pause_duration = 5000ms
  └─ audio_clock_.system_time += 5000ms
  └─ play_start_time_ += 5000ms

// 恢复后查询（系统时间已经过去 6 秒）
GetMasterClock(now = T0+6000ms)
  ├─ AUDIO_MASTER:
  │   └─ elapsed = (T0+6000ms) - (T0+5000ms) = 1000ms
  │   └─ return 1000ms + 1000ms = 2000ms ✅
  │
  └─ EXTERNAL_MASTER:
      └─ elapsed = (T0+6000ms) - (T0+5000ms) = 1000ms
      └─ return 1000ms ✅

// 时钟自动排除了暂停时间！
```

---

## 总结

### 简单且正确的选择策略

```cpp
bool has_audio = audio_decoder_ && audio_decoder_->opened();

if (has_audio) {
  // 有音频 → 用音频时钟（无论有没有视频）
  SetSyncMode(AUDIO_MASTER);
} else {
  // 无音频 → 用系统时钟
  SetSyncMode(EXTERNAL_MASTER);
}
```

这个策略：
- ✅ 覆盖了所有常见场景
- ✅ 简单易懂，不易出错
- ✅ 性能最优，体验最佳
- ✅ 符合用户期望

### 关键理解

1. **AUDIO_MASTER 不仅用于音视频同步**
   - 有音频时，总是用音频时钟（无论有没有视频）
   - 音频时钟由硬件驱动，最稳定

2. **EXTERNAL_MASTER 是无音频时的最佳选择**
   - 系统时钟简单可靠
   - 不要误用 VIDEO_MASTER

3. **VIDEO_MASTER 是特殊场景，慎用**
   - 需要音频重采样支持
   - 可能导致音频卡顿
   - 大多数情况下不需要

---

**文档版本**: v1.0  
**创建时间**: 2025-10-13  
**状态**: 已完成
