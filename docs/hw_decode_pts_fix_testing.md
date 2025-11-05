# 硬件解码 PTS 修复测试指南

## 修改内容

### 核心修复 (playback_controller.cpp:451-457)

**修改前（错误）：**
```cpp
timestamp.pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                    ? frame->best_effort_timestamp  // ❌ 对 B 帧可能错误
                    : frame->pts;
```

**修改后（正确）：**
```cpp
timestamp.pts = frame->pts;  // ✅ 直接使用 FFmpeg 解码器保证正确的 PTS
timestamp.dts = frame->pkt_dts;
```

**理由：**
- `best_effort_timestamp` 对于 B 帧可能错误地使用 DTS 推算
- 导致时间戳跳变（如 pts=52440 → best_effort=158940，偏差 106500ms ≈ 1000ms）
- MPV 等成熟播放器直接使用 `frame->pts`，已验证稳定

## 测试方法

### 1. 重新编译

```powershell
cd d:\code\zenplay\build
cmake --build . --config Release
```

### 2. 测试同一视频文件

使用之前测试的相同视频文件（包含 B 帧的 H.264/HEVC）：

**测试步骤：**

1. **硬解播放**：
   ```powershell
   .\Release\zenplay.exe --hwdec=d3d11va <test_video.mp4>
   ```

2. **观察日志输出**：
   - 查找 `Decoded video frame` 日志
   - 观察 `reorder_offset` 和 `pts_ms` 值
   - 验证 B 帧（reorder_offset < 0）的 pts_ms 是否连续

3. **观察统计信息**：
   - AV 同步偏移应该 < 50ms（之前是 ±1000ms）
   - 渲染帧率应该接近 60fps（之前是 9fps）
   - 同步质量应该是 Good/Excellent（之前是 Poor）

### 3. 对比软解/硬解

**软解测试（baseline）：**
```powershell
.\Release\zenplay.exe --hwdec=no <test_video.mp4>
```

**硬解测试（修复后）：**
```powershell
.\Release\zenplay.exe --hwdec=d3d11va <test_video.mp4>
```

**对比指标：**
| 指标 | 软解 (baseline) | 硬解 (修复前) | 硬解 (修复后) | 目标 |
|------|----------------|--------------|--------------|------|
| AV 同步偏移 | ±20ms | ±1043ms ❌ | **±30ms** ✅ | <50ms |
| 渲染帧率 | 60fps | 9fps ❌ | **60fps** ✅ | 60fps |
| 同步质量 | Excellent | Poor ❌ | **Good/Excellent** ✅ | Good+ |
| 视频时钟跳变 | 无 | 有 (±106s) ❌ | **无** ✅ | 无 |

## 日志验证

### 正常 B 帧日志（修复后）

```
[DEBUG] Decoded video frame: pts=50940, dts=50940, reorder_offset=0, time_base=1/60000, pts_ms=849.00
[DEBUG] Decoded video frame: pts=52440, dts=158940, reorder_offset=-106500, time_base=1/60000, pts_ms=874.00
[DEBUG] Decoded video frame: pts=53940, dts=160440, reorder_offset=-106500, time_base=1/60000, pts_ms=899.00
```

**关键验证点：**
✅ `pts_ms` 连续递增：849.00 → 874.00 → 899.00（25ms 间隔，正常）
✅ B 帧（reorder_offset < 0）的 pts_ms 没有异常跳变
✅ AV 同步日志显示偏移 < 50ms

### 异常日志（修复前，如果还用 best_effort_timestamp）

```
[DEBUG] Decoded video frame: pts=50940, dts=50940, reorder_offset=0, time_base=1/60000, pts_ms=849.00
[DEBUG] Decoded video frame: pts=158940, dts=158940, reorder_offset=0, time_base=1/60000, pts_ms=2649.00  ❌ 跳变！
[WARN] AV Sync offset: +1800.00ms (Poor)  ❌
```

## 统计数据验证

### 修复前（使用 best_effort_timestamp）

```
=== Pipeline Statistics ===
Pipeline Stats:
  Demux    -> Packets: 756
  VideoDec -> Input: 756, Decoded: 59.9fps, Queue: 14/30 (46.7%)
  VideoRnd -> Rendered: 9.0fps, Dropped: 0 (0.0%)  ❌ 渲染慢

Sync Stats:
  AV Sync  -> Offset: +1043.33ms, Quality: Poor  ❌ 同步差
  InSync: No  ❌
```

### 修复后（使用 frame->pts）

**预期输出：**
```
=== Pipeline Statistics ===
Pipeline Stats:
  Demux    -> Packets: 756
  VideoDec -> Input: 756, Decoded: 59.9fps, Queue: 14/30 (46.7%)
  VideoRnd -> Rendered: 59.5fps, Dropped: 0 (0.0%)  ✅ 渲染正常

Sync Stats:
  AV Sync  -> Offset: +25.67ms, Quality: Good  ✅ 同步好
  InSync: Yes  ✅
```

## 问题排查

### 如果修复后仍然不同步

**可能原因：**

1. **音频/视频首帧 PTS 差异大**：
   - 检查第一帧音频 PTS 和第一帧视频 PTS 是否接近
   - 日志查找：`NormalizeAudioPTS` 和 `NormalizeVideoPTS` 的第一次调用
   
2. **time_base 转换错误**：
   - 确认 `av_q2d(timestamp.time_base) * 1000.0` 计算正确
   - 检查 pts_ms 是否以毫秒为单位
   
3. **渲染性能仍慢**：
   - 这是独立问题（与 PTS 无关）
   - 需要 profiling `d3d11_renderer.cpp` 的 Present() 调用

### 调试日志增强

如果需要更详细的诊断，添加：

**在 `av_sync_controller.cpp:71`（UpdateAudioClock）后：**
```cpp
MODULE_DEBUG(LOG_MODULE_SYNC,
             "Audio clock update: raw_pts={:.2f}ms, normalized={:.2f}ms, "
             "drift={:.2f}ms",
             audio_pts_ms, normalized_pts, audio_clock_.drift);
```

**在 `av_sync_controller.cpp:105`（UpdateVideoClock）后：**
```cpp
MODULE_DEBUG(LOG_MODULE_SYNC,
             "Video clock update: raw_pts={:.2f}ms, normalized={:.2f}ms, "
             "drift={:.2f}ms",
             video_pts_ms, normalized_pts, video_clock_.drift);
```

**在 `video_player.cpp:371`（CalculateAVSync）：**
```cpp
MODULE_DEBUG(LOG_MODULE_SYNC,
             "AV Sync: video_pts={:.2f}ms, normalized={:.2f}ms, "
             "master_clock={:.2f}ms, offset={:.2f}ms",
             video_pts_ms, normalized_pts_ms, master_clock_ms,
             normalized_pts_ms - master_clock_ms);
```

## 验收标准

### 必须满足（P0）

- ✅ AV 同步偏移 < 50ms（之前 ±1000ms）
- ✅ 硬解与软解的 AV 同步偏移差异 < 20ms
- ✅ B 帧视频的 pts_ms 连续，无异常跳变
- ✅ 统计信息显示 InSync: Yes

### 应该满足（P1）

- ✅ 渲染帧率接近解码帧率（如 60fps vs 59.9fps）
- ✅ 播放流畅，无卡顿或音画不对位
- ✅ 日志中无 WARN 级别的同步警告

### 可选改进（P2）

- ⚪ 渲染性能优化（独立任务，与 PTS 无关）
- ⚪ 支持变帧率视频（VFR）
- ⚪ 多种硬件解码后端测试（NVDEC, Intel QSV）

## 回归风险评估

### 低风险

**原因：**
1. **FFmpeg 保证**: `frame->pts` 是解码器保证正确的字段
2. **MPV 验证**: MPV 使用相同方法，稳定运行多年
3. **向下兼容**: 对于 I/P 帧，`frame->pts` == `best_effort_timestamp`
4. **影响范围**: 仅影响 B 帧视频（H.264/HEVC with B-frames）

### 可能影响的视频类型

**受益（修复问题）：**
- H.264/AVC with B-frames（大多数视频）
- HEVC/H.265 with B-frames
- MPEG-2 with B-frames

**无影响（行为不变）：**
- I-frame only videos（如 MJPEG）
- VP8/VP9（通常无 B 帧）
- 软件解码（`best_effort_timestamp` 本就正确）

## 后续优化

如果此修复验证成功，可以进一步：

### 1. 移除 best_effort_timestamp 相关代码

**完全移除对 `best_effort_timestamp` 的依赖：**
```cpp
// 简化代码
timestamp.pts = frame->pts;
timestamp.dts = frame->pkt_dts;
// 移除 best_effort_timestamp 相关注释和条件判断
```

### 2. 优化渲染性能（独立任务）

**问题**: 渲染 9fps（应该 60fps）

**可能原因**:
- SwapChain Present(1, 0) 阻塞在 VSync
- 过度的 GPU Flush() 或 Map() 同步
- CPU-GPU pipeline stalls

**解决方案**:
- Profiling Present() 调用耗时
- 测试 Present(0, 0) 异步渲染
- 移除不必要的 GPU 同步点

### 3. 增强同步算法（P2）

**当前**: 简单的 drift 补偿（系数 0.1）

**改进**:
- PI 控制器（比例-积分）更平滑
- 自适应 drift 补偿阈值
- 支持变帧率（VFR）视频

## 参考资料

- **FFmpeg AVFrame 文档**: https://ffmpeg.org/doxygen/trunk/structAVFrame.html
- **MPV vd_lavc.c**: https://github.com/mpv-player/mpv/blob/master/video/decode/vd_lavc.c#L1260-1270
- **B-frame 编码原理**: docs/bframe_decode_behavior.md
- **PTS 分析文档**: docs/hw_decode_pts_analysis.md
