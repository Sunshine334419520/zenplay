/**
 * @file timer_usage_guide.md
 * @brief TimerUtil 使用指南
 */

# TimerUtil 使用指南

## 概述

`TimerUtil` 是一个简单易用的耗时统计工具，支持多种使用方式，所有时间单位统一为毫秒。

## 基本用法

### 1. 最简单的用法

```cpp
TimerUtil timer;  // 构造时开始计时
// ... 执行一些操作
auto elapsed_ms = timer.ElapsedMs();  // 获取耗时（毫秒）
```

### 2. 宏定义用法（推荐）

```cpp
TIMER_START(decode);        // 开始计时
// ... 执行解码操作
auto ms = TIMER_END_MS(decode);  // 结束并获取耗时
```

### 3. 重复测量

```cpp
TimerUtil timer;
// ... 第一次操作
auto first_ms = timer.ElapsedMs();
timer.Reset();  // 重置计时器
// ... 第二次操作
auto second_ms = timer.ElapsedMs();
```

## 实际应用示例

### 在播放控制器中的应用

```cpp
// 1. 测量解封装耗时
TIMER_START(demux_read);
bool success = demuxer_->ReadPacket(&packet);
auto demux_time_ms = TIMER_END_MS_INT(demux_read);
STATS_UPDATE_DEMUX(1, packet->size, demux_time_ms, is_video);

// 2. 测量解码耗时
TIMER_START(video_decode);
video_decoder_->Decode(packet, &frames);
auto decode_ms = TIMER_END_MS(video_decode);
if (decode_ms > 5.0) {
    MODULE_WARN(LOG_MODULE_PLAYER, "视频解码耗时过长: {:.2f} ms", decode_ms);
}

// 3. 测量渲染耗时
TimerUtil render_timer;
renderer_->RenderFrame(frame);
MODULE_INFO(LOG_MODULE_PLAYER, "渲染耗时: {:.2f} ms", render_timer.ElapsedMs());
```

### 在音频播放器中的应用

```cpp
void AudioPlayer::ProcessAudio() {
    // 整个函数的耗时统计
    TimerUtil total_timer;
    
    // 具体操作的耗时统计
    TIMER_START(resample);
    ResampleAudio(input, output);
    auto resample_ms = TIMER_END_MS(resample);
    
    if (resample_ms > 5.0) {
        MODULE_WARN(LOG_MODULE_AUDIO, "音频重采样耗时过长: {:.2f} ms", resample_ms);
    }
    
    auto total_ms = total_timer.ElapsedMs();
    if (total_ms > 10.0) {
        MODULE_WARN(LOG_MODULE_AUDIO, "音频处理总耗时过长: {:.2f} ms", total_ms);
    }
}
```

## API 参考

### TimerUtil 类

- `TimerUtil()`: 构造函数，开始计时
- `void Reset()`: 重新开始计时
- `double ElapsedMs() const`: 获取耗时（毫秒，带小数）
- `int64_t ElapsedMsInt() const`: 获取耗时（毫秒，整数）
- `int64_t ElapsedUs() const`: 获取耗时（微秒）

### 宏定义

- `TIMER_START(name)`: 开始计时
- `TIMER_END_MS(name)`: 结束计时，返回毫秒（带小数）
- `TIMER_END_MS_INT(name)`: 结束计时，返回毫秒（整数）
- `TIMER_END_US(name)`: 结束计时，返回微秒

## 最佳实践

1. **选择合适的方式**：
   - 简单测量：使用宏 `TIMER_START/TIMER_END_MS`
   - 需要多次获取：使用 `TimerUtil` 类
   - 需要重复测量：使用 `TimerUtil` 的 `Reset()` 方法

2. **避免性能开销**：
   - 只在需要时使用，不要在高频调用的地方滥用
   - 合理设置日志级别，避免打印过多日志

3. **命名规范**：
   - 使用有意义的计时器名称
   - 统一命名风格，如 "decode_video", "render_frame" 等

4. **性能考虑**：
   - `std::chrono::steady_clock` 提供高精度计时
   - 计时开销极小，适合在性能敏感的代码中使用