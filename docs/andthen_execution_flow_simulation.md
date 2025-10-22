# 🎬 ZenPlayer::Open() AndThen 执行流程模拟

## 📋 重构前后对比

### ❌ 重构前（传统方式）
```cpp
bool ZenPlayer::Open(const std::string& url) {
  auto demux_result = demuxer_->Open(url);
  if (!demux_result.IsOk()) {
    MODULE_ERROR(...);
    return false;  // 忘记清理？
  }

  if (video_stream) {
    auto video_result = video_decoder_->Open(...);
    if (!video_result.IsOk()) {
      demuxer_->Close();  // ❌ 手动清理（容易遗漏）
      MODULE_ERROR(...);
      return false;
    }
  }

  if (audio_stream) {
    auto audio_result = audio_decoder_->Open(...);
    if (!audio_result.IsOk()) {
      video_decoder_->Close();  // ❌ 必须记住前面的资源
      demuxer_->Close();
      MODULE_ERROR(...);
      return false;
    }
  }
  
  // ... 创建播放控制器
  return true;
}
```

**问题**：
1. 🐛 **易出错**：每个错误路径都要手动清理，容易遗漏
2. 📚 **难维护**：新增步骤需要更新所有错误路径
3. 🔁 **重复代码**：相似的错误处理逻辑重复多次
4. 📖 **可读性差**：深层嵌套，难以理解流程

---

### ✅ 重构后（AndThen 链式调用）
```cpp
Result<void> ZenPlayer::Open(const std::string& url) {
  return demuxer_->Open(url)
      .AndThen([this](auto) { return video_decoder_->Open(...); })
      .AndThen([this](auto) { return audio_decoder_->Open(...); })
      .AndThen([this](auto) { /* 创建控制器 */ return Result<void>::Ok(); })
      .MapErr([this](ErrorCode code) { 
        /* 统一清理 */ 
        Cleanup(); 
        return code; 
      });
}
```

**优势**：
1. ✅ **安全**：统一的清理路径，不会遗漏
2. ✅ **易维护**：新增步骤只需添加一个 `.AndThen()`
3. ✅ **简洁**：消除重复的错误处理代码
4. ✅ **可读**：线性流程，一目了然

---

## 🎯 MapErr 详解

### MapErr 的本质

```cpp
template <typename F>
Result<T> MapErr(F&& f) {
  if (IsOk()) {
    return std::move(*this);  // ✅ 成功时：原样返回
  }
  // ❌ 失败时：应用函数到错误码
  return Result<T>::Err(std::forward<F>(f)(error_code_), message_);
}
```

### MapErr 的两种用法

#### 用法 1：转换错误码
```cpp
.MapErr([](ErrorCode e) {
  // 将所有底层错误统一映射为高层错误
  if (e == ErrorCode::kFileNotFound || e == ErrorCode::kIOError) {
    return ErrorCode::kDemuxError;  // 统一为解封装错误
  }
  return e;
});
```

#### 用法 2：执行清理副作用（本项目使用）
```cpp
.MapErr([this, &url](ErrorCode code) {
  // 🧹 副作用：清理资源
  Cleanup();
  
  // 📝 副作用：记录日志
  MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to open '{}'", url);
  
  // 🔄 副作用：状态转换
  state_manager_->TransitionToError();
  
  // 返回原错误码（不转换）
  return code;
});
```

**关键点**：
- `MapErr` 的 lambda **必须返回一个 ErrorCode**
- 返回的 ErrorCode 会成为新的错误码
- **副作用**（如清理资源、记录日志）在 lambda 内部执行
- 如果不需要转换错误码，直接 `return code;`

---

## 🎬 执行流程模拟

假设打开文件 `test.mp4`：

### 场景 1：✅ 所有步骤成功

```
用户调用：player->Open("test.mp4")

┌─────────────────────────────────────────────────────────────┐
│ Step 0: 准备阶段                                             │
├─────────────────────────────────────────────────────────────┤
│ • 日志：Opening URL: test.mp4                                │
│ • 检查：is_opened_ = false（未打开）                          │
│ • 状态：TransitionToOpening()                                │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 1: demuxer_->Open(url)                                 │
├─────────────────────────────────────────────────────────────┤
│ • FFmpeg avformat_open_input() 成功                          │
│ • FFmpeg avformat_find_stream_info() 成功                    │
│ • 返回：Result<void>::Ok()                                   │
│                                                              │
│ ✅ IsOk() = true → 继续执行 .AndThen()                       │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 2: .AndThen(打开视频解码器)                             │
├─────────────────────────────────────────────────────────────┤
│ • 查找视频流：active_video_stream_index = 0（找到）          │
│ • 日志：Opening video decoder...                             │
│ • video_decoder_->Open(codecpar)                            │
│   - avcodec_find_decoder() 成功                             │
│   - avcodec_open2() 成功                                    │
│ • 返回：Result<void>::Ok()                                   │
│                                                              │
│ ✅ IsOk() = true → 继续执行下一个 .AndThen()                 │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 3: .AndThen(打开音频解码器)                             │
├─────────────────────────────────────────────────────────────┤
│ • 查找音频流：active_audio_stream_index = 1（找到）          │
│ • 日志：Opening audio decoder...                             │
│ • audio_decoder_->Open(codecpar)                            │
│   - avcodec_find_decoder() 成功                             │
│   - avcodec_open2() 成功                                    │
│ • 返回：Result<void>::Ok()                                   │
│                                                              │
│ ✅ IsOk() = true → 继续执行下一个 .AndThen()                 │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 4: .AndThen(创建播放控制器)                             │
├─────────────────────────────────────────────────────────────┤
│ • 日志：Creating playback controller...                      │
│ • playback_controller_ = new PlaybackController(...)        │
│ • is_opened_ = true                                         │
│ • 状态：TransitionToStopped()                                │
│ • 日志：✅ File opened successfully, state: Stopped          │
│ • 返回：Result<void>::Ok()                                   │
│                                                              │
│ ✅ IsOk() = true → 跳过 .MapErr()                            │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 最终结果：Result<void>::Ok()                                 │
├─────────────────────────────────────────────────────────────┤
│ • 返回给调用者：Success                                       │
│ • UI 可以调用 Play() 开始播放                                │
└─────────────────────────────────────────────────────────────┘
```

---

### 场景 2：❌ Step 1 失败（文件不存在）

```
用户调用：player->Open("nonexistent.mp4")

┌─────────────────────────────────────────────────────────────┐
│ Step 0: 准备阶段                                             │
├─────────────────────────────────────────────────────────────┤
│ • 日志：Opening URL: nonexistent.mp4                         │
│ • 状态：TransitionToOpening()                                │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 1: demuxer_->Open(url)                                 │
├─────────────────────────────────────────────────────────────┤
│ • FFmpeg avformat_open_input() 失败：AVERROR(ENOENT)        │
│ • 返回：Result<void>::Err(                                   │
│     ErrorCode::kFileNotFound,                               │
│     "Failed to open input: No such file")                   │
│                                                              │
│ ❌ IsOk() = false → 跳过所有 .AndThen()，直接执行 .MapErr() │
└─────────────────────────────────────────────────────────────┘
                            ↓ (跳过 Step 2, 3, 4)
┌─────────────────────────────────────────────────────────────┐
│ 错误处理：.MapErr()                                          │
├─────────────────────────────────────────────────────────────┤
│ • 接收到错误码：ErrorCode::kFileNotFound                     │
│                                                              │
│ • 🧹 清理资源：                                              │
│   - playback_controller_.reset() (空，跳过)                  │
│   - audio_decoder_->Close() (未打开，跳过)                   │
│   - video_decoder_->Close() (未打开，跳过)                   │
│   - demuxer_->Close() (未完全打开，跳过)                     │
│                                                              │
│ • 📝 记录日志：                                              │
│   MODULE_ERROR(LOG_MODULE_PLAYER,                           │
│     "❌ Failed to open 'nonexistent.mp4': FileNotFound")    │
│                                                              │
│ • 🔄 状态转换：                                              │
│   state_manager_->TransitionToError()                       │
│                                                              │
│ • is_opened_ = false                                        │
│                                                              │
│ • 返回：ErrorCode::kFileNotFound (保持不变)                  │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 最终结果：Result<void>::Err(                                 │
│   ErrorCode::kFileNotFound,                                 │
│   "Failed to open input: No such file"                      │
│ )                                                            │
├─────────────────────────────────────────────────────────────┤
│ • UI 代码检测：result.IsOk() = false                         │
│ • UI 显示错误：                                              │
│   QMessageBox::critical("Error", result.Error().message)    │
└─────────────────────────────────────────────────────────────┘
```

---

### 场景 3：❌ Step 2 失败（视频解码器打开失败）

```
用户调用：player->Open("corrupted.mp4")

┌─────────────────────────────────────────────────────────────┐
│ Step 0: 准备阶段                                             │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 1: demuxer_->Open(url) ✅                               │
├─────────────────────────────────────────────────────────────┤
│ • 文件打开成功，流信息读取成功                                │
│ • 返回：Result<void>::Ok()                                   │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 2: .AndThen(打开视频解码器) ❌                          │
├─────────────────────────────────────────────────────────────┤
│ • 查找视频流：找到（index = 0）                               │
│ • 日志：Opening video decoder...                             │
│ • video_decoder_->Open(codecpar)                            │
│   - avcodec_find_decoder() 成功                             │
│   - avcodec_open2() 失败：AVERROR(EINVAL) [参数损坏]        │
│ • 返回：Result<void>::Err(                                   │
│     ErrorCode::kDecoderInitFailed,                          │
│     "Failed to open codec: Invalid data")                   │
│                                                              │
│ ❌ IsOk() = false → 跳过后续 .AndThen()，执行 .MapErr()     │
└─────────────────────────────────────────────────────────────┘
                            ↓ (跳过 Step 3, 4)
┌─────────────────────────────────────────────────────────────┐
│ 错误处理：.MapErr()                                          │
├─────────────────────────────────────────────────────────────┤
│ • 接收到错误码：ErrorCode::kDecoderInitFailed                │
│                                                              │
│ • 🧹 清理资源：                                              │
│   - playback_controller_.reset() (未创建，跳过)              │
│   - audio_decoder_->Close() (未打开，跳过)                   │
│   - video_decoder_->Close() (打开失败，跳过)                 │
│   - demuxer_->Close() ✅ (已打开，需要关闭！)                │
│                                                              │
│ • 📝 记录日志：                                              │
│   MODULE_ERROR(LOG_MODULE_PLAYER,                           │
│     "❌ Failed to open 'corrupted.mp4': DecoderInitFailed") │
│                                                              │
│ • 🔄 状态转换：TransitionToError()                           │
│ • is_opened_ = false                                        │
│ • 返回：ErrorCode::kDecoderInitFailed                        │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 最终结果：Result<void>::Err(                                 │
│   ErrorCode::kDecoderInitFailed,                            │
│   "Failed to open codec: Invalid data"                      │
│ )                                                            │
└─────────────────────────────────────────────────────────────┘
```

**关键观察**：
- ✅ `demuxer_->Close()` 被自动调用（MapErr 统一清理）
- ✅ 无需在每个 `.AndThen()` 内部手动清理前面的资源
- ✅ 代码更简洁，不会遗漏清理逻辑

---

### 场景 4：❌ Step 3 失败（音频解码器打开失败）

```
用户调用：player->Open("video_only_bad_audio.mp4")

Step 1: demuxer_->Open(url) ✅ 成功
Step 2: video_decoder_->Open() ✅ 成功
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 3: .AndThen(打开音频解码器) ❌                          │
├─────────────────────────────────────────────────────────────┤
│ • 查找音频流：找到（index = 1）                               │
│ • audio_decoder_->Open(codecpar) 失败                        │
│ • 返回：Result<void>::Err(                                   │
│     ErrorCode::kUnsupportedCodec,                           │
│     "Codec not supported")                                  │
└─────────────────────────────────────────────────────────────┘
                            ↓ (跳过 Step 4)
┌─────────────────────────────────────────────────────────────┐
│ 错误处理：.MapErr()                                          │
├─────────────────────────────────────────────────────────────┤
│ • 🧹 清理资源：                                              │
│   - playback_controller_.reset() (未创建)                    │
│   - audio_decoder_->Close() (打开失败，跳过)                 │
│   - video_decoder_->Close() ✅ (已打开，需要关闭！)          │
│   - demuxer_->Close() ✅ (已打开，需要关闭！)                │
│                                                              │
│ • 📝 日志：Failed to open: UnsupportedCodec                  │
│ • 🔄 状态：TransitionToError()                               │
│ • 返回：ErrorCode::kUnsupportedCodec                         │
└─────────────────────────────────────────────────────────────┘
```

**关键观察**：
- ✅ 已成功打开的 `demuxer_` 和 `video_decoder_` 被自动关闭
- ✅ 无需在 Step 3 的 lambda 内部手动清理 Step 1 和 Step 2 的资源

---

## 🔍 AndThen vs 传统方式的资源清理对比

### 传统方式的清理矩阵

| 失败位置 | 需要清理的资源 | 代码位置 |
|---------|---------------|---------|
| Step 1 失败 | 无 | `if (!demux_result.IsOk())` 内 |
| Step 2 失败 | demuxer | `if (!video_result.IsOk())` 内 |
| Step 3 失败 | demuxer + video_decoder | `if (!audio_result.IsOk())` 内 |
| Step 4 失败 | 全部 | `if (!controller_created)` 内 |

**问题**：每个错误路径都要**手动枚举**前面的资源，极易出错！

---

### AndThen 方式的清理矩阵

| 失败位置 | 清理逻辑 | 代码位置 |
|---------|---------|---------|
| 任何步骤失败 | 全部资源（带条件检查） | **统一在 `.MapErr()` 内** |

**优势**：
- ✅ **单一责任**：清理逻辑只在一个地方
- ✅ **防御性编程**：`if (resource && resource->opened())`
- ✅ **易维护**：新增步骤无需修改清理逻辑

---

## 📊 代码行数对比

### 传统方式
```cpp
bool Open(const std::string& url) {
  // ... 准备 ...
  
  auto demux_result = demuxer_->Open(url);
  if (!demux_result.IsOk()) {
    MODULE_ERROR(...);        // 3 行
    return false;             // 1 行
  }                           // 共 5 行
  
  if (video_stream) {
    auto video_result = video_decoder_->Open(...);
    if (!video_result.IsOk()) {
      demuxer_->Close();      // 1 行
      MODULE_ERROR(...);      // 3 行
      return false;           // 1 行
    }                         // 共 7 行
  }
  
  if (audio_stream) {
    auto audio_result = audio_decoder_->Open(...);
    if (!audio_result.IsOk()) {
      video_decoder_->Close();// 1 行
      demuxer_->Close();      // 1 行
      MODULE_ERROR(...);      // 3 行
      return false;           // 1 行
    }                         // 共 8 行
  }
  
  // ... 创建控制器 ...
  return true;
}

// 总计：约 70 行（包括注释和空行）
```

---

### AndThen 方式
```cpp
Result<void> Open(const std::string& url) {
  // ... 准备 ...
  
  return demuxer_->Open(url)
      .AndThen([this](auto) { /* 打开视频 */ })   // 5 行
      .AndThen([this](auto) { /* 打开音频 */ })   // 5 行
      .AndThen([this](auto) { /* 创建控制器 */ }) // 7 行
      .MapErr([this](ErrorCode code) {            // 15 行（统一清理）
        /* 清理所有资源 */
        return code;
      });
}

// 总计：约 50 行（减少 30%）
```

---

## 🎓 关键收益总结

| 维度 | 传统方式 | AndThen 方式 |
|-----|---------|-------------|
| **代码行数** | 70 行 | 50 行 (-30%) |
| **错误处理点** | 3 个独立的 if | 1 个统一的 MapErr |
| **清理逻辑** | 分散在 3 处 | 集中在 1 处 |
| **漏掉清理的风险** | ⚠️ 高 | ✅ 低 |
| **新增步骤成本** | 需更新所有错误路径 | 只需加一个 `.AndThen()` |
| **可读性** | 嵌套深 | 线性流程 |
| **维护性** | 困难 | 简单 |

---

## 🚀 实际使用建议

### 适合使用 AndThen 的场景

1. ✅ **多步骤初始化**：Open, Init, Connect
2. ✅ **资源申请链**：打开文件 → 分配内存 → 创建对象
3. ✅ **状态转换链**：验证 → 解析 → 执行
4. ✅ **数据处理管道**：读取 → 解码 → 转换 → 输出

### 不适合使用 AndThen 的场景

1. ❌ **单步操作**：简单的函数调用（过度设计）
2. ❌ **需要中间值**：每一步的返回值都不同且都需要使用
3. ❌ **并行操作**：多个独立的操作不依赖顺序

---

## 📚 扩展阅读

### Rust Result<T, E> 对比

本项目的 `Result<T>` 设计受 Rust 启发：

```rust
// Rust 版本
fn open(&self, url: &str) -> Result<(), Error> {
    demuxer.open(url)?
        .and_then(|_| video_decoder.open())
        .and_then(|_| audio_decoder.open())
        .and_then(|_| Ok(create_controller()))
        .map_err(|e| {
            cleanup();
            e
        })
}
```

**相似点**：
- `.and_then()` ≈ C++ `.AndThen()`
- `.map_err()` ≈ C++ `.MapErr()`
- `?` 操作符 ≈ C++ 的自动错误传播

**差异点**：
- Rust 是编译期强制，C++ 依赖开发者自律
- Rust 的 `?` 更简洁，C++ 需要显式调用 `.AndThen()`

---

## ✅ 总结

**AndThen 链式调用的本质**：
- 🎯 **成功时**：继续执行下一步
- 🛑 **失败时**：短路跳过后续步骤，直接到 MapErr

**MapErr 的作用**：
- 🧹 **清理资源**（副作用）
- 📝 **记录日志**（副作用）
- 🔄 **转换错误码**（可选）

**最佳实践**：
1. 用 `.AndThen()` 串联多个 `Result<void>` 操作
2. 在最后用 `.MapErr()` 统一处理错误和清理
3. 避免在 `.AndThen()` 内部手动检查前面的状态

通过这种模式，代码更**安全、简洁、易维护**！🎉
