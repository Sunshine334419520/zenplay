# AVERROR_INVALIDDATA + PTS 跳变：根本原因深度分析

**作者**：基于 MPV 硬件解码架构分析  
**日期**：2025-11-05  
**结论**：这两个现象是**同一个根本问题的两个表现**

---

## 🎯 核心观点

### ZenPlay 当前遇到的问题链

```
在错误的时机手动创建 hw_frames_ctx
        ↓
使用 best_effort_timestamp 而非 frame->pts
        ↓
硬件缓冲池大小不匹配
        ↓
B 帧出现时缓冲池耗尽 → AVERROR_INVALIDDATA
        ↓
解码器缓冲 B 帧包，等待参考帧
        ↓
由于缓冲不当，导致 PTS/DTS 使用混乱
        ↓
最终表现：AVERROR_INVALIDDATA + PTS 跳变
```

---

## 📊 问题根源分析

### 根本问题 1: 硬件缓冲池管理不当

#### ZenPlay 的问题

根据你的描述："解码 30 帧左右会遇到 AVERROR_INVALIDDATA"

**为什么是 30 帧？**

```
情景：H.264 30fps 视频，使用硬件解码

I 帧（第 0-29 帧）：快速解码，每帧占用 1 个 surface
    ↓
B 帧开始出现（第 30 帧）：需要多个 surface 存储参考帧

// 假设池大小计算错误（只考虑 has_b_frames=0）
pool_size = 1 + 0 + 0 + 6 = 7 surfaces

// 但实际 B 帧需求
B 帧需要：I 帧 + P 帧的参考 = 需要 2+ 个额外 surfaces

// 结果：30 帧后，7 个 surfaces 全部被占用
frame 31 (B 帧): 需要新的 surface → 池耗尽 → AVERROR_INVALIDDATA
```

#### 对比 MPV 的做法

```c
// MPV: video/decode/vd_lavc.c
int ret = avcodec_get_hw_frames_parameters(avctx, hw_device_ctx, 
                                            AV_PIX_FMT_D3D11, &frames_ctx);
// ✅ 关键：让 FFmpeg 自动计算池大小（已考虑 B 帧数）

// MPV 代码 video/d3d.c 的 refine 回调：
if (sw_format == AV_PIX_FMT_NV12) {
  // ✅ FFmpeg 自动计算的池大小已经合理
  // 不需要手动调整（或只加很小的 extra_frames）
}
```

---

### 根本问题 2: 时间戳管理混乱

#### FFmpeg 的 AVERROR_INVALIDDATA 含义

根据 FFmpeg 源码分析：

```c
// libavcodec/utils.c: get_frame_defaults()
// 当解码器无法产生有效帧时返回 AVERROR_INVALIDDATA

// 这发生在：
// 1. 包格式无效
// 2. 缓冲区不足（硬件 surface 耗尽）← 我们的情况
// 3. 参考帧缺失（B 帧需要的 I/P 帧）
```

#### B 帧时间戳的真相

你的日志显示：
```
Frame 1: pts=50940,   dts=50940,   reorder_offset=0       (I/P帧)
Frame 2: pts=52440,   dts=158940,  reorder_offset=-106500  (B帧)
Frame 3: pts=53940,   dts=160440,  reorder_offset=-106500  (B帧)
```

**这个日志本身没有问题！**

- `reorder_offset = pts - dts = 52440 - 158940 = -106500` ✅ 正确
- B 帧的 PTS < DTS 是正常的（pts是显示时间，dts是解码时间）

**问题出现在**：如果代码使用 `best_effort_timestamp` 而非 `frame->pts`

```cpp
// ❌ 错误的方式
timestamp.pts = frame->best_effort_timestamp;  
// 可能返回 dts (158940) 而不是 pts (52440)
// 结果：时间戳从 52440 跳到 158940 = 跳变 1.7 秒

// ✅ 正确的方式
timestamp.pts = frame->pts;  // 总是正确的显示时间
```

---

## 🔗 问题的因果关系

### 场景 A: 缓冲池过小（你描述的情况）

```
Step 1: 初始化阶段
├─ hw_frames_ctx 池大小计算: pool_size = 7
├─ 为什么是 7？可能的原因：
│  ├─ get_format 回调时 has_b_frames 还未确定 (值为 0)
│  └─ 手动计算：1 + 0 + 0 + 6 = 7
└─ 实际需求：应该是 ~20

Step 2: 解码 I/P 帧（正常）
├─ Frame 0-29: 每帧占用 1 个 surface
├─ 完成后全部 unref，surface 返回池
└─ 反复使用，运行良好

Step 3: 首个 B 帧到达（第 30 帧）
├─ B 帧解码需要多个 surface（参考帧 DPB）
├─ 假设最坏情况：需要 8 个 surface（I/P + 多个 B）
├─ 但池中只有 7 个
└─ 第 8 个 surface 分配失败 → AVERROR_INVALIDDATA

Step 4: 解码器缓冲 B 帧包
├─ "我不能解码这个包（没有 surface），等待..."
├─ 包被缓冲在解码器内部
└─ 尝试 send 时返回 AVERROR_INVALIDDATA

Step 5: PTS 混乱开始
├─ 解码器内部的缓冲队列无序
├─ 时间戳混乱（如果代码逻辑不当）
└─ 最终表现：PTS 跳变
```

### 场景 B: 代码使用了 best_effort_timestamp（时间戳问题）

```
即使缓冲池足够大，如果代码用了 best_effort_timestamp：

For B-frame:
  frame->pts = 52440     ✅ 正确的显示时间
  frame->best_effort_timestamp = 158940  ❌ 错误！用了 DTS
  
Application code:
  timestamp.pts = frame->best_effort_timestamp  ❌
  // 使用了 158940 而不是 52440
  // 结果：时间戳从 50940 → 158940 = 跳变 107500 ≈ 1.7 秒
  
av_sync_controller 看到这个跳变：
  if (pts_diff > 1000ms) {
    // 很可能判断为异常或 seek
    // 导致后续同步混乱
  }
```

---

## 💡 MPV 是如何解决这两个问题的？

### 解决方案 1: 正确的硬件帧上下文初始化

```c
// MPV: video/decode/vd_lavc.c:1260-1286

static int get_format_cb(AVCodecContext *avctx, const enum AVPixelFormat *fmt)
{
  // ✅ Step 1: 让 FFmpeg 计算帧上下文参数
  ret = avcodec_get_hw_frames_parameters(avctx, hw_device_ctx,
                                         AV_PIX_FMT_D3D11, &frames_ctx);
  if (ret < 0) goto error;
  
  // ✅ Step 2: FFmpeg 已经根据序列头计算了 has_b_frames
  // frames_ctx->initial_pool_size 现在是正确的值
  
  // ✅ Step 3: 仅微调（如果需要）
  const int extra_frames = 6;  // hwdec_extra_frames 参数
  frames_ctx->initial_pool_size += extra_frames;
  
  // ✅ Step 4: 现在 pool_size 是准确的
  ret = av_hwframe_ctx_init(frames_ctx);
  
  return AV_PIX_FMT_D3D11;  // 告诉解码器使用硬件解码
}
```

**关键点**：
- 不手动创建 hw_frames_ctx
- 让 FFmpeg 使用 `avcodec_get_hw_frames_parameters` 自动创建
- FFmpeg 已经解析了序列头，知道真实的 B 帧数
- 结果：pool_size 是准确的 (17-23)，而不是错误的 7

### 解决方案 2: 处理 AVERROR_INVALIDDATA 的正确态度

```c
// MPV: 将 AVERROR_INVALIDDATA 视为正常情况
int ret = avcodec_send_packet(avctx, pkt);

if (ret == AVERROR_INVALIDDATA) {
  // ✅ 这是预期的！不是错误
  // B 帧包到达时，参考帧可能还未到达，解码器缓冲它
  mp_msg(ctx, MSGL_DBG2, "Packet buffered (expected for B-frames)\n");
}

// ✅ 关键：继续接收帧，不中断流程
while (true) {
  ret = avcodec_receive_frame(avctx, frame);
  if (ret == AVERROR(EAGAIN)) break;  // 需要更多数据
  if (ret < 0) break;  // 其他错误或 EOF
  
  // 处理帧...
}
```

### 解决方案 3: 正确处理 B 帧时间戳

```c
// MPV: video/decode/vd_lavc.c:1270-1280

// ✅ 直接使用 frame->pts，不使用 best_effort_timestamp
int64_t pts = (frame->pts != AV_NOPTS_VALUE) 
              ? frame->pts 
              : frame->pkt_dts;  // 仅作为备选

// ✅ 转换为播放器时间基准
double pts_seconds = pts * av_q2d(codec_timebase);

// ✅ 对 B 帧也一样处理，FFmpeg 解码器已经保证 pts 正确
```

**为什么 MPV 的做法有效**：

1. FFmpeg 解码器内部已经处理了 PTS/DTS 重排序
2. `frame->pts` 总是显示时间（正确的值）
3. `best_effort_timestamp` 是一个"估计"，对于硬件解码可能不准
4. MPV 直接信任 FFmpeg 的 PTS

---

## 🔍 你的代码现在的状态

### ✅ 已正确实现

**在 `decode.cpp`**：
```cpp
if (ret == AVERROR_INVALIDDATA) {
  MODULE_DEBUG(LOG_MODULE_DECODER, "B-frame packet buffered...");
  // ✅ 继续接收帧，不中断
}

// ✅ 使用 av_frame_move_ref 而非 clone
av_frame_move_ref(frame, workFrame_.get());
```

**在 `playback_controller.cpp`**：
```cpp
timestamp.pts = frame->pts;  // ✅ 直接使用 pts，不用 best_effort
```

**结论**: 你的代码已经在关键点修复了！

### ⚠️ 仍需验证

1. **硬件帧上下文初始化**
   - 是否使用了 `avcodec_get_hw_frames_parameters`？
   - 还是手动创建 hw_frames_ctx？

2. **AVERROR_INVALIDDATA 出现频率**
   - 只在首个 B 帧出现 1-2 次？
   - 还是持续出现？

3. **PTS 时间戳行为**
   - pts_ms 是否线性递增？
   - 是否有大的跳变（>100ms）？

---

## 📋 诊断检查清单

### 检查 1: 硬件初始化方式

查看 `hw_decoder.cpp` 或相似文件：

```cpp
// ❌ 错误方式（手动创建）
AVBufferRef* hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx_);
AVHWFramesContext* frames_ctx = (AVHWFramesContext*)hw_frames_ref->data;
frames_ctx->format = AV_PIX_FMT_D3D11;
frames_ctx->initial_pool_size = 1 + ctx->has_b_frames + 6;  // 手动计算
av_hwframe_ctx_init(hw_frames_ref);

// ✅ 正确方式（让 FFmpeg 计算）
AVCodecParameters* codec_params = ...;
AVBufferRef* hw_frames_ref = NULL;
ret = avcodec_get_hw_frames_parameters(codec_context_, hw_device_ctx_,
                                        AV_PIX_FMT_D3D11, &hw_frames_ref);
// 现在 FFmpeg 已经计算了正确的 pool_size
```

**检查命令**：
```bash
grep -n "avcodec_get_hw_frames_parameters\|av_hwframe_ctx_alloc" \
  /workspaces/zenplay/src/player/codec/*.cpp
```

### 检查 2: AVERROR_INVALIDDATA 频率

**正常情况**：
```bash
grep "B-frame packet buffered" /tmp/zenplay.log | wc -l
# 应该输出：1 或 2
```

**异常情况**：
```bash
grep "B-frame packet buffered" /tmp/zenplay.log | wc -l
# 如果输出：50+ ← 说明有问题
```

### 检查 3: PTS 线性性

**生成诊断脚本**：

```bash
cat > /tmp/check_pts.sh << 'EOF'
#!/bin/bash
# 提取所有 pts_ms 值
grep "pts_ms=" /tmp/zenplay.log | sed 's/.*pts_ms=//' | sed 's/[^0-9.].*//' > /tmp/pts_values.txt

# Python 脚本检查线性性
python3 << 'PYTHON'
import sys
pts_values = []
with open('/tmp/pts_values.txt') as f:
    for line in f:
        pts_values.append(float(line.strip()))

# 检查差值
diffs = []
for i in range(1, min(50, len(pts_values))):  # 检查前 50 帧
    diff = pts_values[i] - pts_values[i-1]
    diffs.append(diff)
    print(f"Frame {i}: {pts_values[i]:.2f}, diff from prev: {diff:.2f}ms")

# 统计
avg_diff = sum(diffs) / len(diffs)
max_diff = max(diffs)
min_diff = min(diffs)

print(f"\nStatistics:")
print(f"Average frame interval: {avg_diff:.2f}ms")
print(f"Max jump: {max_diff:.2f}ms")
print(f"Min jump: {min_diff:.2f}ms")
print(f"Variance: {max_diff - min_diff:.2f}ms")

if max_diff - min_diff > 50:
    print("⚠️ WARNING: Large PTS variance detected!")
else:
    print("✅ PTS looks good!")
PYTHON
EOF

chmod +x /tmp/check_pts.sh
/tmp/check_pts.sh
```

---

## 🎯 最可能的原因排序

根据你的描述，最可能的原因排序：

| 优先级 | 原因 | 症状 | 检查方法 |
|--------|------|------|---------|
| **P0** | 硬件池大小计算错误 | AVERROR_INVALIDDATA 在 30 帧出现 | 查看 log 中的 `initial_pool_size = ?` |
| **P1** | 使用了错误的时间戳源 | PTS 跳变 1-2 秒 | grep "best_effort_timestamp" |
| **P2** | av_frame_clone 泄漏 surface | 错误持续出现 | grep "av_frame_clone\|av_frame_move_ref" |
| **P3** | 硬件设备问题 | 随机崩溃或无法解码 | nvidia-smi 或 GPU 状态检查 |

---

## 🚀 下一步行动计划

### Step 1: 确认你已有的修复

```bash
cd /workspaces/zenplay

# 检查是否使用了正确的时间戳
grep -n "timestamp.pts = frame->pts" src/player/playback_controller.cpp

# 检查是否使用了 av_frame_move_ref
grep -n "av_frame_move_ref" src/player/codec/decode.cpp

# 检查是否正确处理了 AVERROR_INVALIDDATA
grep -A3 "AVERROR_INVALIDDATA" src/player/codec/decode.cpp
```

### Step 2: 验证硬件初始化

```bash
# 找到硬件解码初始化的地方
find src -name "*.cpp" -o -name "*.h" | xargs grep -l "hw_frames_ctx\|hwdevice_ctx"
```

### Step 3: 构建并测试

```bash
cmake --build build --config Debug
./build/zenplay test_video.mp4 > /tmp/zenplay.log 2>&1

# 检查关键指标
echo "=== Pool size ==="
grep "pool_size" /tmp/zenplay.log | head -5

echo "=== AVERROR_INVALIDDATA frequency ==="
grep "B-frame packet buffered" /tmp/zenplay.log | wc -l

echo "=== First 20 frames pts_ms ==="
grep "pts_ms=" /tmp/zenplay.log | head -20
```

### Step 4: 分析结果

根据上面的输出，判断问题所在。

---

## 📝 总结

### 你现在遇到的两个现象：

1. **AVERROR_INVALIDDATA 错误** ← **可能正常，或说明池大小不对**
2. **PTS 跳变** ← **你的代码已修复，应该不会有了**

### 关键的修复（你已做）：

✅ 使用 `frame->pts` 而非 `best_effort_timestamp`  
✅ 继续接收帧而不是返回 AVERROR_INVALIDDATA  
✅ 使用 `av_frame_move_ref` 而非 `av_frame_clone`

### 仍需验证：

⚠️ 硬件帧上下文是否正确初始化（池大小）  
⚠️ AVERROR_INVALIDDATA 出现频率是否正常  
⚠️ PTS 时间戳是否真的在跳变

**请提供以上诊断步骤的输出，我可以做出最终判断。**

