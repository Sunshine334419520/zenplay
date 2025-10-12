# WASAPI 音频初始化错误修复 (0x88890014)

## 问题描述

调用 `IAudioClient::Start()` 时返回错误码 `0x88890014` (`AUDCLNT_E_DEVICE_IN_USE`)。

## 错误码含义

- **错误码**: `0x88890014`
- **符号名**: `AUDCLNT_E_DEVICE_IN_USE`
- **含义**: 音频设备已被占用（通常指独占模式冲突）

## 根本原因

在 `ConfigureAudioFormat()` 中使用了不一致的初始化模式：

```cpp
// ❌ 错误代码
hr = audio_client_->Initialize(
    AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,  // 指定了事件驱动模式
    10000000,
    0,
    wave_format_, 
    nullptr);
```

**问题**：
1. 指定了 `AUDCLNT_STREAMFLAGS_EVENTCALLBACK` 标志，表示使用事件驱动模式
2. 但没有调用 `IAudioClient::SetEventHandle()` 设置事件句柄
3. 实际的 `AudioThreadMain()` 使用的是轮询模式（`GetCurrentPadding()` + `Sleep()`）
4. 模式不匹配导致初始化不完整，某些情况下触发 `DEVICE_IN_USE` 错误

## WASAPI 的两种工作模式

### 1. 轮询模式（Polling Mode）

**特点**：
- 简单易用
- 线程定期查询缓冲区状态
- 适合大多数场景

**实现**：
```cpp
// 初始化时不使用特殊标志
audio_client_->Initialize(
    AUDCLNT_SHAREMODE_SHARED,
    0,  // 不设置任何标志
    10000000,
    0,
    wave_format_, 
    nullptr);

// 线程中轮询
while (!should_stop) {
  UINT32 padding;
  audio_client_->GetCurrentPadding(&padding);
  
  UINT32 available = buffer_size - padding;
  if (available > 0) {
    // 填充音频数据
  }
  
  Sleep(10);  // 定期休眠
}
```

### 2. 事件驱动模式（Event-Driven Mode）

**特点**：
- 更精确的同步
- 更低的延迟
- 实现相对复杂

**实现**：
```cpp
// 1. 创建事件句柄
HANDLE audio_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

// 2. 初始化时指定事件标志
audio_client_->Initialize(
    AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,  // 事件驱动
    10000000,
    0,
    wave_format_, 
    nullptr);

// 3. ⚠️ 必须设置事件句柄
audio_client_->SetEventHandle(audio_event);

// 4. 线程中等待事件
while (!should_stop) {
  WaitForSingleObject(audio_event, INFINITE);  // 等待信号
  
  // 填充音频数据
  // 不需要 Sleep，事件会自动控制节奏
}

// 5. 清理
CloseHandle(audio_event);
```

## 解决方案

### 修复代码

将初始化模式改为轮询模式，与实际的线程实现保持一致：

```cpp
// 修复后的代码
hr = audio_client_->Initialize(
    AUDCLNT_SHAREMODE_SHARED,
    0,  // ✅ 移除事件标志，使用轮询模式
    10000000,  // 1秒缓冲区（100ns 单位）
    0,         // 共享模式下为0
    wave_format_, 
    nullptr);
```

### 为什么这样能解决问题？

1. **模式一致性**：初始化参数与线程实现匹配
2. **无需事件句柄**：轮询模式不需要 `SetEventHandle()`
3. **避免状态冲突**：不会因缺少事件句柄导致设备状态异常

## 其他发现的问题

### 1. 线程安全问题

`Pause()` 和 `Resume()` 在修改音频客户端状态时没有同步：

```cpp
// ⚠️ 潜在问题
void WasapiAudioOutput::Pause() {
  is_paused_ = true;
  if (audio_client_) {
    audio_client_->Stop();  // 可能与 AudioThreadMain 冲突
  }
}

void WasapiAudioOutput::Resume() {
  is_paused_ = false;
  if (audio_client_) {
    audio_client_->Start();  // 可能与 AudioThreadMain 冲突
  }
}
```

**建议**：
- 如果只需要静音效果，使用 `is_paused_` 标志即可，不需要调用 `Stop()/Start()`
- 如果确实需要停止设备，添加互斥锁保护

### 2. 缓冲区时长建议

```cpp
10000000  // 1秒缓冲区，偏大
```

**建议**：
- 视频播放：100-200ms (`1000000` - `2000000`)
- 音乐播放：200-500ms (`2000000` - `5000000`)
- 1秒缓冲区会导致较高的播放延迟

### 3. Sleep 时长优化

```cpp
Sleep(10);  // 10ms
```

对于 44100Hz 采样率，10ms = 441 个采样。可以根据实际缓冲区大小动态调整。

## 测试验证

### 测试步骤

1. 编译修复后的代码
2. 运行播放器
3. 打开任意视频/音频文件
4. 验证音频正常播放
5. 测试暂停/恢复功能

### 预期结果

- ✅ `IAudioClient::Start()` 成功返回
- ✅ 音频流畅播放
- ✅ 无设备占用错误
- ✅ 暂停/恢复功能正常

## WASAPI 最佳实践

### 初始化顺序

```cpp
1. CoInitializeEx()           // 初始化 COM
2. CoCreateInstance()          // 创建设备枚举器
3. GetDefaultAudioEndpoint()   // 获取设备
4. IMMDevice::Activate()       // 激活音频客户端
5. IAudioClient::GetMixFormat() // 获取混合格式（可选）
6. IAudioClient::Initialize()  // 初始化客户端
7. IAudioClient::GetBufferSize() // 获取缓冲区大小
8. IAudioClient::GetService()  // 获取渲染客户端
9. IAudioClient::Start()       // 启动音频流
```

### 清理顺序

```cpp
1. IAudioClient::Stop()        // 停止音频流
2. 等待线程结束
3. Release 所有 COM 接口（倒序释放）
4. CoTaskMemFree(WAVEFORMATEX)
5. CoUninitialize()            // 反初始化 COM
```

### 错误处理

常见 WASAPI 错误码：

| 错误码 | 符号名 | 含义 | 解决方法 |
|--------|--------|------|----------|
| 0x88890001 | AUDCLNT_E_NOT_INITIALIZED | 未初始化 | 先调用 Initialize() |
| 0x88890004 | AUDCLNT_E_ALREADY_INITIALIZED | 重复初始化 | 避免多次 Initialize() |
| 0x88890008 | AUDCLNT_E_UNSUPPORTED_FORMAT | 格式不支持 | 使用 IsFormatSupported() 检查 |
| 0x88890014 | AUDCLNT_E_DEVICE_IN_USE | 设备占用 | 检查独占模式冲突 |
| 0x88890017 | AUDCLNT_E_BUFFER_SIZE_ERROR | 缓冲区大小错误 | 检查缓冲区参数 |

## 参考资料

- [WASAPI 官方文档](https://docs.microsoft.com/en-us/windows/win32/coreaudio/wasapi)
- [IAudioClient Interface](https://docs.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-iaudioclient)
- [Core Audio APIs](https://docs.microsoft.com/en-us/windows/win32/coreaudio/core-audio-apis)

## 受影响的文件

- `src/player/audio/impl/wasapi_audio_output.cpp` - 修改 `ConfigureAudioFormat()` 的初始化参数
