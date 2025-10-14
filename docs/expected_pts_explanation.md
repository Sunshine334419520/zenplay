# UpdateClock 中 expected_pts 推算原理详解

## 🤔 问题

在 `UpdateAudioClock` 和 `UpdateVideoClock` 中有这样的代码:

```cpp
if (audio_clock_.system_time.time_since_epoch().count() > 0) {
  // 根据上次更新的时钟，推算当前应该的PTS
  double expected_pts = audio_clock_.GetCurrentTime(system_time);
  
  // 计算实际PTS与推算PTS的差异
  double drift = normalized_pts - expected_pts;
  
  // 慢速调整drift
  audio_clock_.drift = drift * 0.1;
}
```

**疑问**:
- PTS 为什么可以推断出来?
- 推断的依据是什么?
- 怎么就会是准确的呢?

---

## 💡 核心原理

### 理想假设: 媒体以恒定速率播放

**关键认知**: 
> 在理想情况下,媒体流应该以**恒定速率**播放。
> - 音频: 采样率固定 (如 44100 Hz)
> - 视频: 帧率固定 (如 30 fps)

**推算依据**:
```
如果媒体以恒定速率播放,那么:
  PTS 的增长速度 = 系统时间的流逝速度

即:
  当前 PTS = 上次 PTS + 经过的系统时间
  
  expected_pts = last_pts + (current_time - last_time)
```

---

## 📊 具体例子

### 例子 1: 理想的音频播放

```
假设: 音频采样率 44100 Hz, 每次 callback 处理 1024 samples

T0 时刻:
  UpdateAudioClock(1000ms, T0)
  audio_clock.pts_ms = 1000ms
  audio_clock.system_time = T0

T1 时刻 (23.2ms 后, 因为 1024/44100 ≈ 23.2ms):
  // 音频硬件应该刚好播放完 1024 samples
  
  理论上应该调用:
    UpdateAudioClock(1023.2ms, T1)
  
  推算:
    expected_pts = 1000 + (T1 - T0)
                 = 1000 + 23.2
                 = 1023.2ms
  
  实际传入:
    normalized_pts = 1023.2ms
  
  对比:
    drift = 1023.2 - 1023.2 = 0ms  ← 完美同步!
```

### 例子 2: 音频硬件稍快

```
T0: UpdateAudioClock(1000ms, T0)

T1 (23.2ms 后):
  // 音频硬件稍快,实际已经播放到 1024ms
  
  实际调用:
    UpdateAudioClock(1024ms, T1)
  
  推算:
    expected_pts = 1000 + 23.2 = 1023.2ms
  
  实际:
    normalized_pts = 1024ms
  
  对比:
    drift = 1024 - 1023.2 = 0.8ms  ← 硬件稍快!
    audio_clock.drift = 0.8 * 0.1 = 0.08ms
```

### 例子 3: 音频硬件稍慢

```
T0: UpdateAudioClock(1000ms, T0)

T1 (23.2ms 后):
  // 音频硬件稍慢,实际只播放到 1022ms
  
  实际调用:
    UpdateAudioClock(1022ms, T1)
  
  推算:
    expected_pts = 1000 + 23.2 = 1023.2ms
  
  实际:
    normalized_pts = 1022ms
  
  对比:
    drift = 1022 - 1023.2 = -1.2ms  ← 硬件稍慢!
    audio_clock.drift = -1.2 * 0.1 = -0.12ms
```

---

## 🔍 GetCurrentTime 的实现

```cpp
struct Clock {
  std::atomic<double> pts_ms;
  std::chrono::steady_clock::time_point system_time;
  double drift;
  
  double GetCurrentTime(std::chrono::steady_clock::time_point current_time) const {
    // 计算从上次更新到现在的时间差
    auto elapsed = current_time - system_time;
    double elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
    
    // 推算当前播放位置
    return pts_ms.load() + elapsed_ms + drift;
  }
};
```

**推算公式**:
```
expected_pts = last_pts + elapsed_time + drift

其中:
  last_pts = 上次更新的 PTS
  elapsed_time = current_time - last_system_time (系统时间流逝)
  drift = 时钟漂移补偿
```

---

## ❓ 为什么这个推算"应该"准确?

### 理由 1: 音频硬件的稳定性

**音频播放由硬件晶振驱动**:
- 音频硬件有独立的晶振 (如 44.1 kHz, 48 kHz)
- 晶振频率非常稳定 (误差 < 0.01%)
- 采样率几乎恒定

**结果**:
```
假设采样率 44100 Hz:
  理论: 1 秒播放 44100 samples
  实际: 1 秒播放 44100 ± 4 samples (误差 < 0.01%)

PTS 增长速度 ≈ 系统时间流逝速度
```

### 理由 2: 视频渲染的周期性

**视频以固定帧率渲染**:
- 30 fps → 每帧 33.3ms
- 60 fps → 每帧 16.7ms

**理想情况**:
```
T0: UpdateVideoClock(1000ms, T0)
T1 (33.3ms 后): UpdateVideoClock(1033.3ms, T1)
T2 (33.3ms 后): UpdateVideoClock(1066.6ms, T2)

每次间隔都是 33.3ms,推算非常准确!
```

### 理由 3: 系统时钟的精度

**steady_clock 的特性**:
- 单调递增,不会倒退
- 精度很高 (通常 ns 级别)
- 不受系统时间调整影响

**结果**:
```
elapsed_time = current_time - last_time  ← 非常准确!
```

---

## ⚠️ 但实际上并不总是准确!

### 不准确的原因 1: 解码延迟波动

```
理想情况:
  T0: decode frame1 → 10ms → UpdateClock(1000ms, T0)
  T1: decode frame2 → 10ms → UpdateClock(1033ms, T0+43ms)
  间隔: 43ms (33ms 帧间隔 + 10ms 解码)

实际情况:
  T0: decode frame1 → 10ms → UpdateClock(1000ms, T0)
  T1: decode frame2 → 25ms → UpdateClock(1033ms, T0+58ms)  ← 解码慢了!
  间隔: 58ms

推算:
  expected_pts = 1000 + 58 = 1058ms
  actual_pts = 1033ms
  drift = 1033 - 1058 = -25ms  ← 检测到延迟!
```

### 不准确的原因 2: 系统负载

```
高负载场景:
  T0: UpdateClock(1000ms, T0)
  
  // 系统卡顿 100ms
  
  T1 (实际过了 133ms):
    UpdateClock(1033ms, T1)
  
  推算:
    expected_pts = 1000 + 133 = 1133ms
  
  实际:
    actual_pts = 1033ms
  
  drift = 1033 - 1133 = -100ms  ← 系统卡顿!
```

### 不准确的原因 3: 音频硬件漂移

```
音频硬件实际采样率可能不是精确的 44100 Hz:
  
  标称: 44100 Hz
  实际: 44110 Hz (快了 0.02%)

长时间播放后:
  T0: 0ms
  T1000s 后:
    理论 PTS: 1000s
    实际 PTS: 1000.2s  ← 快了 200ms!
  
  drift 会慢慢累积
```

---

## 🎯 drift 的作用: 补偿误差

### drift 计算公式

```cpp
// 计算实际PTS与推算PTS的差异
double drift = normalized_pts - expected_pts;

// 慢速调整drift（系数0.1），避免时钟突然跳变
audio_clock_.drift = drift * 0.1;
```

### 为什么用 0.1 系数?

**慢速调整的原因**:

```
场景: 偶尔的解码延迟

不使用系数 (drift = 直接差值):
  T0: drift = 0ms
  T1: 偶然延迟 → drift = -20ms  ← 时钟突然跳变!
  T2: 恢复正常 → drift = 0ms   ← 又跳回来!
  
  结果: 时钟抖动,音视频不稳定

使用 0.1 系数 (drift *= 0.1):
  T0: drift = 0ms
  T1: 偶然延迟 → drift = -20 * 0.1 = -2ms  ← 缓慢调整
  T2: 恢复正常 → drift = -2 * 0.9 = -1.8ms  ← 缓慢恢复
  T3: drift = -1.8 * 0.9 = -1.6ms
  ...
  T10: drift ≈ 0ms
  
  结果: 时钟平滑,音视频稳定
```

### drift 的效果

```cpp
double GetCurrentTime(current_time) const {
  auto elapsed = current_time - system_time;
  double elapsed_ms = duration_cast<milliseconds>(elapsed).count();
  
  // drift 提供小幅补偿
  return pts_ms.load() + elapsed_ms + drift;
  //                                   ↑
  //                         补偿硬件/系统的微小误差
}
```

**例子**:
```
音频硬件稍快 (每秒快 10ms):

经过 10 秒:
  累积的 drift ≈ 10ms
  
GetCurrentTime():
  return 10000 + 0 + 10 = 10010ms
  
作用: 补偿了硬件的 10ms 偏快
```

---

## 📊 完整流程示例

### 正常播放场景

```
初始状态:
  audio_clock.pts_ms = 0
  audio_clock.system_time = T0
  audio_clock.drift = 0

第1次更新 (T0 + 23ms):
  实际调用: UpdateAudioClock(23ms, T0+23ms)
  
  推算:
    expected_pts = 0 + 23 + 0 = 23ms
  
  实际:
    normalized_pts = 23ms
  
  drift:
    drift = (23 - 23) * 0.1 = 0ms
  
  更新:
    audio_clock.pts_ms = 23ms
    audio_clock.system_time = T0 + 23ms
    audio_clock.drift = 0ms

第2次更新 (T0 + 46ms):
  实际调用: UpdateAudioClock(46ms, T0+46ms)
  
  推算:
    expected_pts = 23 + 23 + 0 = 46ms
  
  实际:
    normalized_pts = 46ms
  
  drift:
    drift = (46 - 46) * 0.1 = 0ms
  
  更新:
    audio_clock.pts_ms = 46ms
    audio_clock.system_time = T0 + 46ms
    audio_clock.drift = 0ms
```

### 硬件稍快场景

```
第1次更新 (T0 + 23ms):
  实际调用: UpdateAudioClock(23ms, T0+23ms)
  drift = 0ms

第2次更新 (T0 + 46ms):
  // 硬件稍快,实际播放到 47ms
  实际调用: UpdateAudioClock(47ms, T0+46ms)
  
  推算:
    expected_pts = 23 + 23 + 0 = 46ms
  
  实际:
    normalized_pts = 47ms
  
  drift:
    drift = (47 - 46) * 0.1 = 0.1ms  ← 检测到硬件快了 1ms
  
  更新:
    audio_clock.drift = 0.1ms

第3次更新 (T0 + 69ms):
  实际调用: UpdateAudioClock(70ms, T0+69ms)
  
  推算:
    expected_pts = 47 + 23 + 0.1 = 70.1ms  ← drift 参与计算
  
  实际:
    normalized_pts = 70ms
  
  drift:
    drift = (70 - 70.1) * 0.1 = -0.01ms  ← 误差在缩小
  
  更新:
    audio_clock.drift = -0.01ms  ← 接近 0
```

---

## 🎯 总结

### 为什么可以推算 PTS?

1. **理想假设**: 媒体以恒定速率播放
   - 音频: 采样率固定
   - 视频: 帧率固定

2. **推算公式**: `expected_pts = last_pts + elapsed_time + drift`
   - `elapsed_time`: 系统时间流逝 (非常准确)
   - `drift`: 累积的硬件偏差补偿

3. **硬件稳定性**: 音频硬件晶振非常稳定
   - 误差 < 0.01%
   - PTS 增长 ≈ 时间流逝

### 为什么"应该"准确?

1. ✅ **音频硬件**: 晶振驱动,速率恒定
2. ✅ **视频帧率**: 周期性渲染,间隔固定
3. ✅ **系统时钟**: 精度高,单调递增

### 为什么实际可能不准?

1. ⚠️ **解码延迟**: 波动导致间隔不均
2. ⚠️ **系统负载**: 卡顿导致时间异常
3. ⚠️ **硬件漂移**: 长时间累积误差

### drift 的作用

1. **检测偏差**: `drift = actual - expected`
2. **补偿误差**: `GetCurrentTime` 加上 drift
3. **平滑调整**: 使用 0.1 系数避免抖动

---

## 🔧 设计意图

这个机制的目的**不是**让 expected_pts 100% 准确,而是:

1. **检测异常**: 发现硬件/系统的偏差
2. **平滑补偿**: 缓慢调整 drift,避免突变
3. **保持稳定**: 即使有小误差,也能保持音视频同步

**关键**: 
> 推算不需要完美准确,只需要足够接近真实值,
> drift 会自动补偿小误差,保持时钟稳定!

---

## 💡 类比

**汽车定速巡航**:

```
设定: 100 km/h

推算当前位置:
  expected_position = last_position + (current_time - last_time) * 100
  
实际测量:
  actual_position = GPS 测量
  
偏差:
  drift = actual - expected
  
  如果 drift > 0: 车速稍快,减速
  如果 drift < 0: 车速稍慢,加速

结果:
  虽然每次测量有误差,但通过不断调整,
  长期保持在 100 km/h 附近!
```

**时钟推算也是同样的道理**! ✅
