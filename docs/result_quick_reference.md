# Result 和 ErrorCode 快速参考指南

## 📚 文件位置

```
src/player/common/error.h     - 核心实现（529 行）
src/player/common/error.cpp   - 扩展点（10 行）
tests/test_result_error.cpp   - 单元测试（638 行）
```

## 🚀 5 分钟快速开始

### 1. 基础 - 成功和失败

```cpp
#include "player/common/error.h"
using namespace zenplay::player;

// 返回成功结果
Result<int> r1 = Result<int>::Ok(42);
assert(r1.IsOk());
assert(r1.Value() == 42);

// 返回失败结果
Result<int> r2 = Result<int>::Err(ErrorCode::INVALID_PARAM, "must > 0");
assert(!r2.IsOk());
assert(r2.Code() == ErrorCode::INVALID_PARAM);
assert(r2.Message() == "must > 0");
```

### 2. 链式操作 - AndThen

```cpp
// 链式调用，任一失败则停止
auto r = Result<int>::Ok(5)
  .AndThen([](int v) { return Result<int>::Ok(v * 2); })
  .AndThen([](int v) { return Result<int>::Ok(v + 10); });
// 结果：Ok(20)
```

### 3. 类型转换 - Map

```cpp
auto r = Result<int>::Ok(42)
  .Map([](int v) { return std::to_string(v); });
// 结果：Ok("42")，类型为 Result<std::string>
```

### 4. 错误恢复 - OrElse

```cpp
auto r = Result<int>::Err(ErrorCode::IO_ERROR)
  .OrElse([](ErrorCode e) {
    if (e == ErrorCode::IO_ERROR) {
      return Result<int>::Ok(0);  // 使用默认值
    }
    return Result<int>::Err(e);
  });
// 结果：Ok(0)
```

### 5. 无返回值 - Result<void>

```cpp
Result<void> Init() {
  if (!ValidConfig()) {
    return Result<void>::Err(ErrorCode::INVALID_PARAM, "bad config");
  }
  return Result<void>::Ok();
}

auto r = Init();
if (!r.IsOk()) {
  std::cerr << r.Message() << std::endl;
}
```

---

## 💻 常见用法模式

### 模式 1：值验证

```cpp
Result<int> Parse(const std::string& s) {
  try {
    int v = std::stoi(s);
    if (v < 0) {
      return Result<int>::Err(ErrorCode::INVALID_PARAM, "must >= 0");
    }
    return Result<int>::Ok(v);
  } catch (...) {
    return Result<int>::Err(ErrorCode::UNKNOWN, "parse failed");
  }
}

// 使用
auto r = Parse("42").Map([](int v) { return v * 2; });
```

### 模式 2：资源管理

```cpp
Result<std::unique_ptr<File>> OpenFile(const std::string& path) {
  auto file = std::make_unique<File>();
  if (!file->Open(path)) {
    return Result<std::unique_ptr<File>>::Err(
        ErrorCode::IO_ERROR, "cannot open file");
  }
  return Result<std::unique_ptr<File>>::Ok(std::move(file));
}

// 使用 - 所有权转移
auto r = OpenFile("data.bin");
if (r.IsOk()) {
  auto file = r.TakeValue();
  // 现在拥有 file 的所有权
}
```

### 模式 3：初始化顺序

```cpp
Result<void> InitializePlayer() {
  return OpenDecoder()
    .AndThen([this](auto decoder) { 
      decoder_ = std::move(decoder);
      return InitializeAudio();
    })
    .AndThen([this]() { 
      return InitializeVideo();
    })
    .AndThen([this]() {
      return StartRender();
    });
}
```

### 模式 4：错误映射

```cpp
auto r = DatabaseQuery()
  .MapErr([](ErrorCode e) {
    if (e == ErrorCode::NETWORK_ERROR) {
      return ErrorCode::TIMEOUT;  // 映射错误
    }
    return e;
  });
```

### 模式 5：与日志集成

```cpp
auto result = CriticalOperation();
if (!result.IsOk()) {
  MODULE_ERROR("Operation failed: {} (code: {})",
               result.Message(), static_cast<int>(result.Code()));
  return;
}
```

---

## 🔍 常见错误处理

### ❌ 错误 1：拷贝 Result

```cpp
// ❌ 编译错误：Result 禁用拷贝构造
Result<int> r1 = Result<int>::Ok(42);
Result<int> r2 = r1;  // 编译错误！

// ✅ 正确：使用移动
Result<int> r2 = std::move(r1);
```

### ❌ 错误 2：在 Err 后访问 Value

```cpp
auto r = Result<int>::Err(ErrorCode::INVALID_PARAM);
// ❌ 未定义行为
// int v = r.Value();  // 不保证有效值

// ✅ 正确：先检查
if (r.IsOk()) {
  int v = r.Value();
}

// ✅ 或使用默认值
int v = r.ValueOr(0);
```

### ❌ 错误 3：忘记检查结果

```cpp
// ❌ 不检查结果，可能使用无效值
auto r = Operation();
int value = r.Value();

// ✅ 正确：检查后再使用
auto r = Operation();
if (!r.IsOk()) {
  return HandleError(r.Code());
}
int value = r.Value();
```

---

## 📋 错误码速查表

### 通用错误（0-99）

| 代码 | 名称 | 用途 |
|------|------|------|
| 0 | OK | 成功 |
| 1 | INVALID_PARAM | 无效参数 |
| 2 | NOT_INITIALIZED | 未初始化 |
| 3 | ALREADY_RUNNING | 已运行 |
| 99 | UNKNOWN | 未知错误 |

### IO 错误（100-199）

| 代码 | 名称 | 用途 |
|------|------|------|
| 100 | IO_ERROR | 文件操作错误 |
| 101 | INVALID_FILE_FORMAT | 文件格式错误 |
| 102 | STREAM_NOT_FOUND | 流未找到 |
| 103 | DEMUX_ERROR | 解封装错误 |

### 解码错误（200-299）

| 代码 | 名称 | 用途 |
|------|------|------|
| 200 | DECODER_ERROR | 通用解码错误 |
| 201 | DECODER_NOT_FOUND | 解码器未找到 |
| 202 | UNSUPPORTED_CODEC | 不支持的编码 |
| 203 | DECODER_INIT_FAILED | 初始化失败 |
| 204 | DECODER_SEND_FRAME_FAILED | 发送数据失败 |
| 205 | DECODER_RECEIVE_FRAME_FAILED | 接收数据失败 |

### 音频错误（400-499）

| 代码 | 名称 | 用途 |
|------|------|------|
| 400 | AUDIO_ERROR | 通用音频错误 |
| 401 | AUDIO_OUTPUT_ERROR | 音频输出错误 |
| 402 | AUDIO_FORMAT_NOT_SUPPORTED | 不支持的格式 |
| 403 | AUDIO_RESAMPLE_ERROR | 重采样错误 |
| 404 | AUDIO_DEVICE_NOT_FOUND | 设备未找到 |
| 405 | AUDIO_DEVICE_INIT_FAILED | 设备初始化失败 |

### 网络错误（500-599）

| 代码 | 名称 | 用途 |
|------|------|------|
| 500 | NETWORK_ERROR | 通用网络错误 |
| 501 | CONNECTION_TIMEOUT | 连接超时 |
| 502 | CONNECTION_REFUSED | 连接被拒绝 |
| 503 | INVALID_URL | 无效 URL |
| 504 | NETWORK_UNREACHABLE | 网络不可达 |

---

## 🎯 API 参考

### Result<T> 主要方法

```cpp
// 构造
static Result<T> Ok(T value);
static Result<T> Err(ErrorCode code, std::string msg = "");

// 查询
bool IsOk() const;                    // 是否成功
bool IsErr() const;                   // 是否失败
ErrorCode Code() const;               // 获取错误码
const std::string& Message() const;   // 获取错误消息
const char* CodeString() const;       // 获取错误码字符串

// 值访问
T& Value();                           // 可变引用（仅当 IsOk）
const T& Value() const;               // 不可变引用
T TakeValue();                        // 转移所有权
T ValueOr(T default_value) const;     // 或默认值

// 链式操作
template <typename F>
auto AndThen(F&& f);                  // 链式调用

template <typename F>
auto Map(F&& f);                      // 映射值

template <typename F>
auto OrElse(F&& f);                   // 错误恢复

template <typename F>
auto MapErr(F&& f);                   // 映射错误

// 便利
std::string FullMessage() const;      // 完整消息
operator<<(ostream, Result);          // 输出流
```

### 常见类型别名

```cpp
using VoidResult = Result<void>;
```

---

## 🧪 运行测试

```bash
# 编译项目
cmake --preset conan-default
cmake --build build/Debug

# 运行单元测试
cd build/Debug
./tests/zenplay_tests --gtest_filter=ResultErrorTest.*

# 查看特定测试
./tests/zenplay_tests --gtest_filter=ResultErrorTest.ResultOkConstruction

# 显示详细信息
./tests/zenplay_tests --gtest_filter=ResultErrorTest.* -v
```

---

## 💡 最佳实践

### ✅ DO（应该做）

1. **使用 Result 替代 bool**
   ```cpp
   // ✅ 好
   Result<void> Init() { return VoidResult::Ok(); }
   ```

2. **链式操作进行类型转换**
   ```cpp
   // ✅ 好
   auto r = Parse(str)
     .Map(Validate)
     .Map(Transform);
   ```

3. **错误消息要有意义**
   ```cpp
   // ✅ 好
   return Result<T>::Err(ErrorCode::IO_ERROR, 
                         "file not found: " + path);
   ```

4. **在值上施加所有权操作时使用 TakeValue**
   ```cpp
   // ✅ 好
   auto decoder = result.TakeValue();  // 转移 unique_ptr
   ```

### ❌ DON'T（不应该做）

1. **不要在 Err 结果上访问 Value**
   ```cpp
   // ❌ 避免
   auto r = Operation();
   int v = r.Value();  // 只有在 IsOk 时才安全
   ```

2. **不要忽视错误结果**
   ```cpp
   // ❌ 避免
   Operation();  // 忽略返回值
   
   // ✅ 使用明确的放弃
   (void)Operation();  // 意图明确
   ```

3. **不要试图拷贝 Result**
   ```cpp
   // ❌ 避免
   Result<T> r2 = r1;  // 编译错误
   ```

4. **不要嵌套过深的 AndThen**
   ```cpp
   // ❌ 避免：金字塔形代码
   r.AndThen([](a) { return r1.AndThen([](b) { ... }); });
   
   // ✅ 优先：扁平结构
   r.AndThen([](a) { return something(a); })
    .AndThen([](b) { return another(b); });
   ```

---

## 📖 完整文档

详见：`docs/task2_error_handling_implementation_report.md`

## 🔗 相关资源

- Rust Result：https://doc.rust-lang.org/std/result/
- C++ Expected 提案：http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0323r7.pdf

