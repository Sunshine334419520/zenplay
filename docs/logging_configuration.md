# ZenPlay 日志系统配置说明

## 📋 日志级别概述

ZenPlay 使用 **spdlog** 日志库，支持以下级别（从低到高）：

| 级别 | 宏 | 用途 |
|------|-----|------|
| **TRACE** | `ZENPLAY_TRACE()` | 极详细的跟踪信息（函数进出、循环迭代） |
| **DEBUG** | `ZENPLAY_DEBUG()` | 调试信息（变量值、状态变化） |
| **INFO** | `ZENPLAY_INFO()` | 一般信息（启动、关闭、重要操作） |
| **WARN** | `ZENPLAY_WARN()` | 警告（可恢复的问题） |
| **ERROR** | `ZENPLAY_ERROR()` | 错误（功能失败） |
| **CRITICAL** | `ZENPLAY_CRITICAL()` | 严重错误（程序崩溃） |

---

## 🎯 两级过滤机制

### 1️⃣ **编译期过滤**（CMakeLists.txt）

**配置位置**：`CMakeLists.txt` 中的 `SPDLOG_ACTIVE_LEVEL`

```cmake
# Debug 模式：编译所有级别的日志代码
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
# Release 模式：只编译 INFO 及以上
else()
    add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()
```

**作用**：
- ✅ 决定哪些日志**代码**会被编译进可执行文件
- ✅ 低于此级别的日志会被**完全移除**（零性能开销）
- ✅ Release 版本可以减小程序体积

**性能影响**：
```cpp
// 如果 SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO
ZENPLAY_DEBUG("Processing frame {}", frame_id);  // ← 这行代码不会被编译

// 编译后等同于：
// (空代码，完全不存在)
```

---

### 2️⃣ **运行期过滤**（main.cpp）

**配置位置**：`main.cpp` 中的 `LogManager::Initialize()`

```cpp
#ifdef NDEBUG
  // Release 模式：只输出 INFO 及以上
  LogManager::Initialize(LogManager::LogLevel::INFO);
#else
  // Debug 模式：输出 DEBUG 及以上
  LogManager::Initialize(LogManager::LogLevel::DEBUG);
#endif
```

**作用**：
- ✅ 决定哪些**已编译**的日志会被实际输出
- ✅ 可以在运行时动态修改：`LogManager::SetLogLevel(LogLevel::TRACE)`
- ✅ 不能输出比编译期级别更低的日志

**示例**：
```cpp
// 编译期：SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE（所有代码都编译了）
// 运行期：Initialize(LogLevel::INFO)（只输出 INFO 及以上）

ZENPLAY_TRACE("Trace message");  // ❌ 不输出（被运行期过滤）
ZENPLAY_DEBUG("Debug message");  // ❌ 不输出（被运行期过滤）
ZENPLAY_INFO("Info message");    // ✅ 输出
ZENPLAY_WARN("Warn message");    // ✅ 输出
```

---

## 🔄 配置组合效果

| 编译期级别 | 运行期级别 | DEBUG 是否输出？ | 原因 |
|-----------|-----------|-----------------|------|
| `TRACE` | `DEBUG` | ✅ 输出 | 编译了且未过滤 |
| `TRACE` | `INFO` | ❌ 不输出 | 编译了但被运行期过滤 |
| `INFO` | `DEBUG` | ❌ 不输出 | 根本没编译 |
| `INFO` | `INFO` | ❌ 不输出 | 根本没编译 |

**结论**：必须**两个级别都满足**，日志才会输出。

---

## 📊 当前项目配置

### Debug 模式
```
编译期：SPDLOG_LEVEL_TRACE  ← 编译所有日志代码
运行期：LogLevel::DEBUG      ← 输出 DEBUG 及以上

结果：可以看到 DEBUG/INFO/WARN/ERROR/CRITICAL 日志
```

### Release 模式
```
编译期：SPDLOG_LEVEL_INFO    ← 只编译 INFO 及以上
运行期：LogLevel::INFO        ← 输出 INFO 及以上

结果：只能看到 INFO/WARN/ERROR/CRITICAL 日志
      DEBUG/TRACE 代码已被移除，零性能开销
```

---

## 🛠️ 如何临时修改日志级别

### 方法 1：运行时动态修改（推荐）
```cpp
// 在程序运行中任意位置调用
zenplay::LogManager::SetLogLevel(zenplay::LogManager::LogLevel::TRACE);
```

**限制**：只能显示**已编译**的日志级别

---

### 方法 2：修改 main.cpp 重新编译
```cpp
// main.cpp
LogManager::Initialize(LogManager::LogLevel::TRACE);  // 改为 TRACE
```

重新编译即可。

---

### 方法 3：完全开放所有日志（调试用）
```cmake
# CMakeLists.txt - 强制编译所有级别
add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
```
```cpp
// main.cpp - 强制输出所有级别
LogManager::Initialize(LogManager::LogLevel::TRACE);
```

重新配置并编译：
```bash
cmake --build build --config Debug
```

---

## 🎯 推荐使用场景

| 场景 | 编译期 | 运行期 | 说明 |
|------|--------|--------|------|
| **日常开发** | `TRACE` | `DEBUG` | 看到所有调试信息 |
| **性能测试** | `TRACE` | `INFO` | 减少日志 I/O 影响 |
| **Release 发布** | `INFO` | `INFO` | 减小程序体积 |
| **Debug 排查** | `TRACE` | `TRACE` | 看到最详细信息 |

---

## 📝 日志输出位置

### 1. 控制台输出
- **Windows Debug 模式**：自动显示控制台窗口
- **Windows Release 模式**：不显示控制台（纯 GUI）

### 2. 文件输出
- **路径**：`<程序目录>/logs/zenplay.log`
- **轮转策略**：单个文件最大 5MB，保留最近 3 个文件
- **级别**：文件始终记录 `TRACE` 及以上（不受运行期级别限制）

---

## ⚡ 性能建议

### Debug 构建
```cpp
// ✅ 随意使用 DEBUG/TRACE 日志
ZENPLAY_DEBUG("Frame {} decoded, size: {}", frame_id, frame_size);
```

### Release 构建
```cpp
// ❌ 避免在热点路径使用 INFO 日志
for (int i = 0; i < 1000000; i++) {
    ZENPLAY_INFO("Processing {}", i);  // ← 会严重影响性能！
}

// ✅ 使用更高级别或减少频率
if (i % 10000 == 0) {
    ZENPLAY_INFO("Progress: {}/1000000", i);
}
```

---

## 🐛 常见问题

### Q1: 设置了 `DEBUG` 级别但看不到 DEBUG 日志？

**原因**：编译期级别限制了（Release 模式默认只编译 INFO 及以上）

**解决**：
```bash
# 使用 Debug 模式构建
cmake --build build --config Debug
```

---

### Q2: Windows 上看不到控制台窗口？

**原因**：Release 模式默认是 GUI 应用

**解决**：
1. 使用 Debug 模式运行（自动显示控制台）
2. 查看文件日志：`build/Debug/logs/zenplay.log`

---

### Q3: 如何在 Release 模式也显示控制台？

**修改 CMakeLists.txt**：
```cmake
# 强制显示控制台（不推荐，仅调试用）
add_executable(${PROJECT_NAME} ${SRC_FILES} ${QRC_FILES})
```

---

## 📚 相关文件

- **日志管理器**：`src/player/common/log_manager.h/cpp`
- **CMake 配置**：`CMakeLists.txt`（第 22-38 行）
- **初始化代码**：`src/main.cpp`（第 27-40 行）
- **spdlog 文档**：https://github.com/gabime/spdlog

---

## 🔧 高级用法

### 模块化日志
```cpp
// 为特定模块创建日志器
auto video_logger = LogManager::GetModuleLogger(LOG_MODULE_VIDEO);
MODULE_DEBUG(LOG_MODULE_VIDEO, "Video frame decoded");
```

### 动态调整级别
```cpp
// 在程序运行中切换到 TRACE 级别
LogManager::SetLogLevel(LogManager::LogLevel::TRACE);
// ... 调试代码 ...
// 切换回 INFO 级别
LogManager::SetLogLevel(LogManager::LogLevel::INFO);
```

---

**最后更新**：2025-10-06  
**维护者**：ZenPlay Team
