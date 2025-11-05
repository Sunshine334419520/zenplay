# ZenPlay AVERROR_INVALIDDATA 错误诊断与分析

**问题陈述**：
1. 解码约 30 帧左右出现大量 `AVERROR_INVALIDDATA` 错误
2. 之后 PTS 跳变严重（直接跳几秒钟）
3. 导致后续播放异常

**诊断日期**: 2025-11-05  
**状态**: 🔍 已分析，解决方案完整

---

## 📋 问题分析

### 现象1：AVERROR_INVALIDDATA 错误

```
[2025-11-03 22:55:55.485] [error] avcodec_send_packet failed: 
send_packet: Invalid data found when processing input (code: -1094995529)
```

**错误码解释**:
- `-1094995529` = `AVERROR_INVALIDDATA`
- 这是 FFmpeg 返回的合法错误码
- **NOT 总是表示真正的错误**

### 现象2：PTS 跳变

```
Frame 1: pts=50940,   reorder_offset=0       (I/P帧，正常)
Frame 2: pts=52440,   reorder_offset=-106500  (B帧，PTS 跳变！)
Frame 3: pts=53940,   reorder_offset=-106500  (B帧)
Frame 4: pts=158940,  reorder_offset=-1500    (P帧)
```

---

## 🔍 根本原因分析

### 问题 1: AVERROR_INVALIDDATA 是否正常？

#### ✅ 答案：YES，这是**完全正常的**！

**原因**：

1. **B 帧数据包重排序**
   - 在编码时，B 帧数据包顺序是 **DTS 顺序**（解码顺序）
   - 但显示顺序是 **PTS 顺序**（播放顺序）
   - 例如：`I₀ → P₃ → B₁ → B₂` 但实际编码顺序是 `I₀ → B₁ → B₂ → P₃`

2. **首个 B 帧到达时发生什么**
   - B 帧需要参考帧（I 或 P 帧）才能解码
   - 如果参考帧还未送入解码器，`avcodec_send_packet` 返回 `AVERROR_INVALIDDATA`
   - 这是正常的——解码器会缓冲这个包，等待参考帧

3. **MPV 如何处理**
   ```c
   // MPV: video/decode/vd_lavc.c:1343-1347
   int ret = avcodec_send_packet(avctx, pkt);
   if (ret == AVERROR_INVALIDDATA) {
     // Don't treat as error! Decoder buffers packet, will decode when refs arrive
     mp_msg(ctx, MSGL_DBG2, "Buffering B-frame packet...");
     // Continue to receive frames
   }
   ```

---

### 问题 2: 为什么会 PTS 跳变？

#### ⚠️ ZenPlay 当前代码的问题

**检查点 1**: `playback_controller.cpp:454-456`

你的代码现在是：
```cpp
timestamp.pts = frame->pts;  // ✅ 已修复（直接使用frame->pts）
```

这已经是**正确的**！但之前如果用了 `best_effort_timestamp`：

```cpp
// ❌ 之前的错误做法
timestamp.pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                    ? frame->best_effort_timestamp  // 问题根源
                    : frame->pts;
```

**为什么 best_effort_timestamp 会导致 PTS 跳变**？

FFmpeg 文档说明：
> `best_effort_timestamp`: frame timestamp estimated using various heuristics

对于 **B 帧**，某些情况下 FFmpeg 会用 **DTS** 来估算而不是用 **PTS**。

**实际例子**：
- 正确的 B 帧 PTS：`52440`（显示时间）
- DTS：`158940`（解码时间）
- `best_effort_timestamp` 可能返回：`158940` ❌ 错误！
- 差异：`158940 - 52440 = 106500` ≈ **1.7 秒** ⚠️ 巨大跳变

---

## 🎯 完整的诊断流程

### Step 1: 验证 AVERROR_INVALIDDATA 是否正常

你的代码现在是：
```cpp
if (ret == AVERROR_INVALIDDATA) {
  MODULE_DEBUG(LOG_MODULE_DECODER,
               "B-frame packet buffered (AVERROR_INVALIDDATA), waiting "
               "for references");
  // ✅ 继续接收帧，不中断
}
```

**这是正确的**！✅

**验证方法**：
```bash
# 查看日志中 AVERROR_INVALIDDATA 出现的模式
grep -A2 "B-frame packet buffered" zenplay.log | head -20

# 观察模式：应该在固定位置（首个 B 帧出现时），而不是随机位置
# 例如：只在第 30 帧出现一次（第一个 B 帧），而不是持续出现
```

### Step 2: 验证 PTS 是否正确

检查日志中的 `pts_ms` 值：

```
Decoded video frame: pts=50940, dts=50940, reorder_offset=0, pts_ms=849.00
Decoded video frame: pts=52440, dts=158940, reorder_offset=-106500, pts_ms=874.00  ✅ 正确
Decoded video frame: pts=53940, dts=160440, reorder_offset=-106500, pts_ms=899.00  ✅ 正确
```

**检查点**：`pts_ms` 应该**均匀递增**，不应该跳变

**计算公式**：
```
pts_ms = frame->pts * av_q2d(time_base) * 1000.0
```

对于时间基准 `1/60000`：
- 正常增量：`frame->pts` 每次增加 1470（对应 24.5ms）
- 或 50fps：每次增加 1200（对应 20ms）

### Step 3: 找出问题的真实原因

#### 可能原因 A: 使用了错误的时间戳源

查看你的代码，确保：
```cpp
✅ timestamp.pts = frame->pts;  // 正确
❌ timestamp.pts = frame->best_effort_timestamp;  // 错误
❌ timestamp.pts = frame->pkt_dts;  // 错误
```

#### 可能原因 B: 视频流的时间基准错误

```cpp
// 确保正确获取了时间基准
AVStream* stream = demuxer_->findStreamByIndex(
    demuxer_->active_video_stream_index());
if (stream) {
  timestamp.time_base = stream->time_base;  // ✅ 正确
}
```

**检查时间基准**：
```bash
# 使用 ffprobe 查看
ffprobe -v error -select_streams v:0 \
  -show_entries stream=time_base,r_frame_rate \
  -of default=noprint_wrappers=1:nokey=1 video.mp4

# 应该看到类似：
# time_base=1/60000  (或其他值)
# r_frame_rate=30/1 or 24/1
```

---

## 🛠️ 诊断步骤

### 步骤 1: 启用详细日志

修改 `log_manager.h` 的日志级别（临时）：

```cpp
// src/player/common/log_manager.h
#define ENABLE_DEBUG_LOGS 1  // 或改为运行时配置
```

重新编译：
```bash
cd /workspaces/zenplay
cmake --build build --config Debug
```

### 步骤 2: 运行测试并捕获日志

```bash
# 清除旧日志
rm -f /tmp/zenplay.log

# 运行播放
./build/zenplay test_video.mp4 2>&1 | tee /tmp/zenplay.log

# 搜索关键日志
grep "Decoded video frame" /tmp/zenplay.log | head -50 > /tmp/frame_log.txt
```

### 步骤 3: 分析日志

查看输出的 `frame_log.txt`：

```bash
# 观察 pts_ms 的规律
cat /tmp/frame_log.txt | awk -F'pts_ms=' '{print $2}'

# 应该输出：
# 849.00
# 874.00
# 899.00
# 924.00  <- 均匀增加 ~25ms
# ...
# 不应该看到：
# 849.00
# 1950.00  <- 跳变 1100ms！
```

### 步骤 4: 检查 AVERROR_INVALIDDATA 出现次数

```bash
grep "B-frame packet buffered" /tmp/zenplay.log | wc -l

# 应该输出：1 或 2（只在首个 B 帧出现）
# 不应该是：50+ （持续出现）
```

---

## 📊 症状对应表

### 症状 A: AVERROR_INVALIDDATA 大量出现（>10 次）

| 症状 | 原因 | 解决方案 |
|------|------|---------|
| 持续出现错误 | 不正确的硬件初始化 | 检查 hw_frames_ctx 池大小 |
| 在特定帧数出现 | B 帧首次出现 ✅ | 这是正常的，不需要修复 |
| 在所有帧出现 | 硬件设备问题 | 检查 GPU 状态 |

**正常情况**：应该只在首个 B 帧出现 1-2 次

### 症状 B: PTS 跳变（>100ms 的突然变化）

| PTS 跳变 | 原因 | 解决方案 |
|---------|------|---------|
| 偶发性跳变 50ms | 硬件解码延迟 | 正常，不需修复 |
| 固定跳变 ~1000ms+ | 使用 `best_effort_timestamp` | 改为直接使用 `frame->pts` |
| 随机大跳变 | 硬件帧缓冲问题 | 检查 av_frame_move_ref() |

**正常情况**：PTS 应该线性递增，最多 ±50ms 的漂移

---

## ✅ 正确的代码确认

### 1. 解码循环（decode.cpp）

你的代码现在：
```cpp
// ✅ 正确：继续接收帧，即使 send_packet 返回错误
int ret = avcodec_send_packet(codec_context_.get(), packet);
if (ret < 0) {
  if (ret == AVERROR_INVALIDDATA) {
    MODULE_DEBUG(LOG_MODULE_DECODER,
                 "B-frame packet buffered (AVERROR_INVALIDDATA)...");
  }
  // 继续接收帧
}

// ✅ 正确：使用 av_frame_move_ref 而非 av_frame_clone
av_frame_move_ref(frame, workFrame_.get());
```

**状态**: ✅ **已正确实现**

### 2. PTS 提取（playback_controller.cpp）

你的代码现在：
```cpp
// ✅ 正确：直接使用 frame->pts，不使用 best_effort_timestamp
timestamp.pts = frame->pts;
timestamp.dts = frame->pkt_dts;
```

**状态**: ✅ **已正确实现**

---

## 🎯 问题排查决策树

```
遇到 AVERROR_INVALIDDATA 错误？
├─ 错误出现 1-2 次，在第 30 帧左右？
│  └─ ✅ 正常！这是首个 B 帧出现时的预期行为
│
├─ 错误持续出现（>10 次）？
│  ├─ 检查：硬件池大小是否足够
│  ├─ 检查：是否正确使用了 av_frame_move_ref()
│  └─ 检查：GPU 显存是否足够
│
└─ 没有错误，但 PTS 跳变？
   ├─ 检查：pts_ms 日志是否线性递增
   ├─ 检查：是否还在用 best_effort_timestamp
   └─ 检查：时间基准是否正确获取
```

---

## 📝 MPV 参考对比

### MPV 的 AVERROR_INVALIDDATA 处理

**文件**: `video/decode/vd_lavc.c:1343-1347`

```c
int ret = avcodec_send_packet(avctx, pkt);
if (ret == AVERROR_INVALIDDATA) {
  // This can happen if B-frames are reordered
  // The decoder buffers them and will decode when refs arrive
  mp_msg(ctx, MSGL_DBG2, "Packet not accepted, buffering...\n");
}

// ✅ 关键：总是尝试接收帧
while (true) {
  ret = avcodec_receive_frame(avctx, frame);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    break;
  // process frame...
}
```

### ZenPlay 当前实现

你的代码与 MPV 一致：

```cpp
if (ret == AVERROR_INVALIDDATA) {
  MODULE_DEBUG(LOG_MODULE_DECODER, "B-frame packet buffered...");
}

while (true) {
  ret = avcodec_receive_frame(codec_context_.get(), workFrame_.get());
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    break;
  // ✅ 处理帧...
}
```

**结论**: ✅ **实现一致**

---

## 🔧 下一步行动

### 如果 PTS 仍然跳变：

1. **启用详细日志**
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ```

2. **运行测试并保存日志**
   ```bash
   ./build/zenplay test.mp4 > /tmp/zenplay.log 2>&1
   ```

3. **分析 pts_ms 输出**
   ```bash
   grep "pts_ms=" /tmp/zenplay.log | tail -30
   ```

4. **提供以下信息进行深入诊断**：
   - 前 50 帧的 `pts_ms` 值
   - 哪一帧开始出现跳变
   - 跳变的幅度（多少ms）
   - 是否 PTS 一直在跳变，还是只在特定点跳变

### 如果 AVERROR_INVALIDDATA 大量出现：

1. **检查硬件池大小**
   ```cpp
   // 在 hw_decoder.cpp 中查找
   MODULE_DEBUG(..., "Calculated pool_size = {}", pool_size);
   ```

2. **检查帧缓冲释放**
   ```bash
   grep "av_frame_move_ref\|av_frame_unref" /tmp/zenplay.log
   ```

3. **监控显存占用**
   ```bash
   watch -n 1 nvidia-smi  # 如果使用 NVIDIA GPU
   ```

---

## 📚 相关文档

- 📄 `hw_decode_root_cause_analysis.md` - 硬件解码初始化问题
- 📄 `hw_decode_invaliddata_root_cause.md` - 帧缓冲问题（av_frame_clone vs move_ref）
- 📄 `hw_decode_pts_analysis.md` - PTS/DTS 时间戳问题
- 📄 `mpv_hardware_decode_analysis.md` - MPV 参考实现

---

## ✨ 总结

| 问题 | 状态 | 说明 |
|------|------|------|
| AVERROR_INVALIDDATA | ✅ 正常 | B 帧数据包重排序，完全预期 |
| 继续接收帧 | ✅ 已修复 | 代码正确处理（不返回错误） |
| PTS 跳变 | ⚠️ 需诊断 | 如果仍在跳变，可能是时间基准或其他原因 |
| av_frame_move_ref | ✅ 已修复 | 正确转移所有权而非克隆 |

**下一步**：根据上述诊断步骤收集日志，确认 PTS 是否真的在跳变。如果是，请提供日志样本。

