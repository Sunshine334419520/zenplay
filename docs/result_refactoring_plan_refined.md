# ZenPlay Result<T> 重构计划（精简版）

**版本**: v2.0（精简）  
**日期**: 2025-10-21  
**原则**: **只在真正需要错误处理的地方使用 Result<T>**

---

## 🎯 核心原则

### ✅ 应该使用 Result<T> 的场景

1. **I/O 操作可能失败**
   - 打开文件、网络请求、读写数据
   - 例：`Demuxer::Open()`, `Demuxer::ReadPacket()`

2. **资源初始化可能失败**
   - 创建解码器、渲染器、音频设备
   - 例：`Decoder::Open()`, `AudioOutput::Initialize()`

3. **外部依赖调用可能失败**
   - FFmpeg、SDL、WASAPI 等外部库
   - 例：`Renderer::Initialize()`, `AudioOutput::Start()`

4. **需要向上传播错误原因**
   - 调用方需要知道具体失败原因
   - 例：`ZenPlayer::Open()` 需要告诉 UI 是网络错误还是格式错误

### ❌ 不应该使用 Result<T> 的场景

1. **void 返回值且不会失败**
   - 简单的状态更新、通知
   - 例：`Stop()`, `Pause()`, `Resume()`, `Close()`

2. **Getter/Setter**
   - 简单的获取/设置操作
   - 例：`GetDuration()`, `GetCurrentTime()`, `SetVolume()`

3. **回调函数**
   - 状态变更回调、事件通知
   - 例：`RegisterStateChangeCallback()`, `OnWindowResize()`

4. **已有其他错误处理机制**
   - 通过状态机管理的操作（状态转换到 Error 状态）
   - 例：`SeekAsync()` 通过状态回调通知错误

---

## 📊 重构范围对比

### 原计划 vs 精简版

| 模块 | 原计划方法数 | 精简版方法数 | 减少 |
|------|------------|------------|------|
| ZenPlayer | 9 | 3 | -6 |
| PlaybackController | 5 | 1 | -4 |
| AudioPlayer | 6 | 2 | -4 |
| VideoPlayer | 4 | 1 | -3 |
| Demuxer | 5 | 2 | -3 |
| Decoder | 4 | 2 | -2 |
| Renderer | 4 | 1 | -3 |
| AudioOutput | 6 | 2 | -4 |
| **总计** | **43** | **14** | **-29 (67%)** |

---

## 📋 精简重构清单

### 1. ZenPlayer（只改 3 个方法）

#### ✅ 需要改为 Result<T>

```cpp
// ✅ Open - I/O 操作，可能失败（文件不存在、格式错误、网络错误等）
Result<void> Open(const std::string& url);

// ✅ SetRenderWindow - 初始化渲染器，可能失败
Result<void> SetRenderWindow(void* window_handle, int width, int height);

// ✅ Play - 启动播放线程，可能失败
Result<void> Play();
```

#### ❌ 保持原样（void 返回）

```cpp
// ❌ Close - 清理操作，不会失败
void Close();

// ❌ OnWindowResize - 通知操作，不会失败
void OnWindowResize(int width, int height);

// ❌ Pause - 状态切换，不会失败（状态机保证）
void Pause();

// ❌ Stop - 停止操作，不会失败
void Stop();

// ❌ SeekAsync - 异步操作，通过回调通知结果
void SeekAsync(int64_t timestamp_ms, bool backward = true);

// ❌ Getters - 简单查询，不会失败
int64_t GetDuration() const;
int64_t GetCurrentPlayTime() const;
PlayerState GetState() const;
```

---

### 2. Demuxer（只改 2 个方法）

#### ✅ 需要改为 Result<T>

```cpp
// ✅ Open - I/O 操作，可能失败
Result<void> Open(const std::string& url);

// ✅ ReadPacket - I/O 操作，可能失败（EOF、读取错误）
Result<AVPacket*> ReadPacket();
```

#### ❌ 保持原样

```cpp
// ❌ Close - 清理操作
void Close();

// ❌ Seek - 返回 bool 足够（只需要知道成功/失败，不需要详细原因）
bool Seek(int64_t timestamp, bool backward = false);

// ❌ Getters
int64_t GetDuration() const;
int active_video_stream_index() const;
```

---

### 3. Decoder（只改 2 个方法）

#### ✅ 需要改为 Result<T>

```cpp
// ✅ Open - 初始化解码器，可能失败
Result<void> Open(AVCodecParameters* codec_params, AVDictionary** options = nullptr);

// ✅ ReceiveFrame - 解码操作，可能失败
Result<AVFrame*> ReceiveFrame();
```

#### ❌ 保持原样

```cpp
// ❌ Close - 清理操作
void Close();

// ❌ SendPacket - 返回 bool 足够（只是推送，失败很少见）
bool SendPacket(AVPacket* packet);
```

---

### 4. Renderer（只改 1 个方法）

#### ✅ 需要改为 Result<T>

```cpp
// ✅ Initialize - SDL 初始化，可能失败
Result<void> Initialize(void* window_handle, int width, int height);
```

#### ❌ 保持原样

```cpp
// ❌ Cleanup - 清理操作
void Cleanup();

// ❌ RenderFrame - 渲染失败很少见，且不影响播放流程
bool RenderFrame(AVFrame* frame);

// ❌ SetViewport - 简单更新
void SetViewport(int width, int height);
```

---

### 5. AudioOutput（只改 2 个方法）

#### ✅ 需要改为 Result<T>

```cpp
// ✅ Initialize - WASAPI 初始化，可能失败
Result<void> Initialize(const AudioFormat& format);

// ✅ Start - 启动音频设备，可能失败
Result<void> Start();
```

#### ❌ 保持原样

```cpp
// ❌ Stop, Pause, Resume, Flush - 简单状态切换
void Stop();
void Pause();
void Resume();
void Flush();
```

---

### 6. AudioPlayer（只改 2 个方法）

#### ✅ 需要改为 Result<T>

```cpp
// ✅ Init - 初始化音频输出，可能失败
Result<void> Init(const AudioConfig& config);

// ✅ Start - 启动播放线程，可能失败
Result<void> Start();
```

#### ❌ 保持原样

```cpp
// ❌ Stop, Pause, Resume, Flush - 状态切换
void Stop();
void Pause();
void Resume();
void Flush();
```

---

### 7. VideoPlayer（只改 1 个方法）

#### ✅ 需要改为 Result<T>

```cpp
// ✅ Start - 启动渲染线程，可能失败
Result<void> Start();
```

#### ❌ 保持原样

```cpp
// ❌ Init - 只是保存引用，不会失败
void Init(Renderer* renderer, const VideoConfig& config);

// ❌ Stop, Pause, Resume - 状态切换
void Stop();
void Pause();
void Resume();
```

---

### 8. PlaybackController（只改 1 个方法）

#### ✅ 需要改为 Result<T>

```cpp
// ✅ Start - 启动多个线程，可能失败
Result<void> Start();
```

#### ❌ 保持原样

```cpp
// ❌ Stop, Pause, Resume - 状态切换
void Stop();
void Pause();
void Resume();

// ❌ SeekAsync - 异步操作，通过状态机通知结果
void SeekAsync(int64_t timestamp_ms, bool backward);
```

---

## 🔄 重构示例

### 示例 1: ZenPlayer::Open()

#### Before (现状)

```cpp
bool ZenPlayer::Open(const std::string& url) {
  if (!demuxer_->Open(url)) {
    return false;  // 不知道为什么失败
  }
  
  if (!video_decoder_->Open(/* ... */)) {
    return false;  // 不知道为什么失败
  }
  
  return true;
}
```

#### After (精简版)

```cpp
Result<void> ZenPlayer::Open(const std::string& url) {
  // Open 操作可能失败，使用 Result
  auto demux_result = demuxer_->Open(url);
  if (!demux_result) {
    return demux_result.Error();  // 传播详细错误
  }
  
  auto decoder_result = video_decoder_->Open(/* ... */);
  if (!decoder_result) {
    return decoder_result.Error();
  }
  
  return Result<void>::Ok();
}
```

---

### 示例 2: ZenPlayer::Play()

#### Before

```cpp
bool ZenPlayer::Play() {
  if (!playback_controller_->Start()) {
    return false;
  }
  state_manager_->SetState(PlayerState::kPlaying);
  return true;
}
```

#### After

```cpp
Result<void> ZenPlayer::Play() {
  // Start 可能失败（线程启动失败）
  auto result = playback_controller_->Start();
  if (!result) {
    return result;  // 传播错误
  }
  
  state_manager_->SetState(PlayerState::kPlaying);
  return Result<void>::Ok();
}
```

---

### 示例 3: ZenPlayer::Pause()

#### 保持原样（void 返回）

```cpp
void ZenPlayer::Pause() {
  playback_controller_->Pause();  // 不会失败
  state_manager_->SetState(PlayerState::kPaused);
}
```

**为什么不用 Result？**
- Pause 只是状态切换，不会失败
- 状态机保证了合法性
- 不需要向上传播错误

---

### 示例 4: Demuxer::Seek()

#### 保持原样（bool 返回）

```cpp
bool Demuxer::Seek(int64_t timestamp, bool backward) {
  int ret = av_seek_frame(format_context_, -1, timestamp, flags);
  if (ret < 0) {
    SPDLOG_ERROR("Seek failed: {}", av_err2str(ret));
    return false;
  }
  return true;
}
```

**为什么不用 Result？**
- Seek 失败只需要知道成功/失败
- 错误已经通过日志记录
- 调用方只需要重试或忽略
- 不需要向上传播详细错误原因

---

## 📅 执行计划（精简版）

### 第 1 周：基础设施

| 任务 | 工时 | 说明 |
|------|------|------|
| FFmpeg 错误映射 | 2h | MapFFmpegError, FormatFFmpegError |
| WASAPI 错误映射 | 2h | MapHRESULT, FormatHRESULT |
| 便利宏 | 1h | RETURN_IF_ERROR 等 |
| 单元测试 | 2h | 测试错误映射 |

**总计**: 7 小时

---

### 第 2 周：底层模块（4 个方法）

| 模块 | 方法 | 工时 |
|------|------|------|
| Demuxer | Open, ReadPacket | 3h |
| Decoder | Open, ReceiveFrame | 3h |

**总计**: 6 小时

---

### 第 3 周：中层模块（6 个方法）

| 模块 | 方法 | 工时 |
|------|------|------|
| Renderer | Initialize | 2h |
| AudioOutput | Initialize, Start | 3h |
| AudioPlayer | Init, Start | 2h |
| VideoPlayer | Start | 2h |
| PlaybackController | Start | 2h |

**总计**: 11 小时

---

### 第 4 周：顶层集成（3 个方法）

| 模块 | 方法 | 工时 |
|------|------|------|
| ZenPlayer | Open, SetRenderWindow, Play | 4h |
| UI 更新 | 错误显示 | 3h |
| 集成测试 | 完整流程 | 5h |
| 文档更新 | API 文档 | 3h |

**总计**: 15 小时

---

## 📊 总工时对比

| 计划 | 方法数 | 总工时 | 节省 |
|------|--------|--------|------|
| 原计划 | 43 个方法 | 73.5 小时 | - |
| 精简版 | 14 个方法 | 39 小时 | -34.5h (47%) |

---

## ✅ 验收标准（精简版）

### 功能验收

- [ ] 14 个关键方法已改为 Result<T>
- [ ] 所有 Result 方法有清晰的错误信息
- [ ] 29 个方法保持原样（void/bool 返回）
- [ ] UI 能正确显示错误信息

### 质量验收

- [ ] 单元测试覆盖率 > 85%
- [ ] 性能下降 < 5%
- [ ] 无内存泄漏
- [ ] 无编译警告

### 可维护性验收

- [ ] 代码更简洁（不过度使用 Result）
- [ ] 错误处理逻辑清晰
- [ ] API 文档完整

---

## 🎯 设计决策记录

### 决策 1: Pause/Stop/Resume 不使用 Result

**原因**:
1. 这些操作是状态切换，不会失败
2. PlayerStateManager 状态机保证了合法性
3. 如果真的出错，状态机会切换到 Error 状态
4. void 返回值更简洁，符合直觉

**例外**: 如果未来需要详细错误原因，可以再改

---

### 决策 2: SeekAsync 不使用 Result

**原因**:
1. 是异步操作，立即返回
2. 错误通过状态回调通知（kSeeking → kError）
3. 调用方已经通过 RegisterStateChangeCallback 处理错误
4. 返回 Result 反而会造成混淆（同步返回 vs 异步结果）

---

### 决策 3: Seek 保持 bool 返回

**原因**:
1. Seek 失败只需要知道成功/失败
2. 调用方只是简单重试或忽略
3. 详细错误已通过日志记录
4. 不需要向上传播详细原因

**例外**: 如果 UI 需要显示"Seek 失败原因"，可以改为 Result

---

### 决策 4: GetDuration/GetCurrentTime 保持原样

**原因**:
1. 是简单查询，不会失败
2. 返回 -1 表示无效已经足够
3. 不需要详细错误信息

---

## 📝 向后兼容策略

### 不需要向后兼容层

**原因**:
- 只改了 14 个方法，影响面小
- 大部分方法保持原样
- UI 层只需要更新 3 处调用

### UI 更新示例

#### Before

```cpp
void MainWindow::OnOpenFile() {
  QString file = QFileDialog::getOpenFileName(/* ... */);
  if (!player_->Open(file.toStdString())) {
    QMessageBox::critical(this, "Error", "Failed to open file");
  }
}
```

#### After

```cpp
void MainWindow::OnOpenFile() {
  QString file = QFileDialog::getOpenFileName(/* ... */);
  auto result = player_->Open(file.toStdString());
  if (!result) {
    QString error_msg = QString::fromStdString(result.Error().Message());
    QMessageBox::critical(this, "Error", error_msg);
  }
}
```

---

## 🔍 风险评估（精简版）

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 性能下降 | 中 | 低 | 只改 14 个方法，影响小 |
| 编译错误 | 低 | 低 | 影响面小，易修复 |
| 逻辑错误 | 中 | 低 | 单元测试覆盖 |
| 工期延误 | 低 | 低 | 工时减半，39 小时 |

---

## 📖 附录：快速决策表

当你不确定是否使用 Result<T> 时，参考这个表：

| 场景 | 使用 Result<T>？ | 原因 |
|------|----------------|------|
| 打开文件/网络 | ✅ 是 | I/O 操作可能失败，需要错误原因 |
| 初始化硬件设备 | ✅ 是 | 可能失败，需要错误原因 |
| 启动线程 | ✅ 是 | 可能失败，需要错误原因 |
| 解码数据 | ✅ 是 | 可能失败，需要错误原因 |
| Pause/Stop/Resume | ❌ 否 | 状态切换，不会失败 |
| Close/Cleanup | ❌ 否 | 清理操作，不会失败 |
| Getter/Setter | ❌ 否 | 简单查询，不会失败 |
| 异步操作 (xxxAsync) | ❌ 否 | 通过回调通知结果 |
| 只需要知道成功/失败 | ❌ 否 | bool 足够 |
| 回调函数 | ❌ 否 | 不是操作本身 |

---

**文档版本**: v2.0（精简版）  
**最后更新**: 2025-10-21  
**审核状态**: 待审核

