# Zenplay 单元测试文档

本目录包含 Zenplay 播放器的单元测试，使用 Google Test (GTest) 框架实现。

## 📋 测试覆盖范围

基于 [execution_plan_priority_features.md](../docs/execution_plan_priority_features.md) 的优化项实现：

### 1. ThreadSafeQueue 测试 (`test_thread_safe_queue.cpp`)

**测试目标**：验证任务 4 - 条件变量通知机制

- ✅ 基本 Push/Pop 操作
- ✅ 超时与阻塞行为
- ✅ Stop 机制（唤醒所有阻塞线程）
- ✅ Clear 操作（含自定义清理回调）
- ✅ 多线程并发安全性（4 生产者 + 4 消费者）
- ✅ 并发 Stop 信号测试
- ✅ 移动语义（`std::unique_ptr` 支持）
- ⏱️ 性能基准测试（DISABLED，手动运行）

**验收标准**：
- 所有测试通过
- 并发测试无数据丢失或重复
- Stop 能在 100ms 内唤醒所有阻塞线程

### 2. AVSyncController 测试 (`test_av_sync_controller.cpp`)

**测试目标**：验证任务 3 - 音视频同步时钟优化

- ✅ 基础功能（初始化、模式切换、Reset）
- ✅ PTS 归一化（音频/视频独立归一化）
- ✅ 时钟推算（基于系统时间的时钟增长）
- ✅ 暂停/恢复机制（时钟冻结与恢复）
- ✅ 多次暂停/恢复循环
- ✅ 视频延迟计算（同步、超前、落后场景）
- ✅ 丢帧/重复帧决策
- ✅ 外部时钟模式
- ✅ 边界条件（负数 PTS、PTS 跳跃）
- ⏱️ 性能压力测试（10000 帧，DISABLED）

**验收标准**：
- 所有测试通过
- PTS 归一化误差 < 1ms
- 时钟推算误差 < 50ms（考虑系统调度）
- 暂停期间时钟不增长
- 每帧处理时间 < 1ms（性能测试）

---

## 🛠️ 构建与运行

### 前置条件

确保已通过 Conan 安装依赖（包括 GTest）：

```powershell
# 安装依赖
conan install . --output-folder=build --build=missing

# 配置 CMake（启用测试）
cmake -B build -DBUILD_TESTING=ON

# 构建主项目和测试
cmake --build build --config Debug
```

### 运行所有测试

**方法 1：使用 CTest**

```powershell
cd build
ctest --output-on-failure --config Debug
```

**方法 2：直接运行测试可执行文件**

```powershell
.\build\tests\Debug\zenplay_tests.exe
```

### 运行特定测试

```powershell
# 运行所有 ThreadSafeQueue 测试
.\build\tests\Debug\zenplay_tests.exe --gtest_filter=ThreadSafeQueueTest.*

# 运行所有 AVSyncController 测试
.\build\tests\Debug\zenplay_tests.exe --gtest_filter=AVSyncControllerTest.*

# 运行单个测试
.\build\tests\Debug\zenplay_tests.exe --gtest_filter=ThreadSafeQueueTest.BasicPushPop
```

### 运行性能测试（默认禁用）

```powershell
# 性能基准测试（带 DISABLED_ 前缀）
.\build\tests\Debug\zenplay_tests.exe --gtest_also_run_disabled_tests --gtest_filter=*Performance*
```

---

## 📊 测试输出示例

### 成功输出

```
[==========] Running 35 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 15 tests from ThreadSafeQueueTest
[ RUN      ] ThreadSafeQueueTest.BasicPushPop
[       OK ] ThreadSafeQueueTest.BasicPushPop (2 ms)
[ RUN      ] ThreadSafeQueueTest.ConcurrentPushPop
[       OK ] ThreadSafeQueueTest.ConcurrentPushPop (1534 ms)
...
[----------] 20 tests from AVSyncControllerTest
[ RUN      ] AVSyncControllerTest.AudioPTSNormalization
[       OK ] AVSyncControllerTest.AudioPTSNormalization (12 ms)
[ RUN      ] AVSyncControllerTest.PauseResume
[       OK ] AVSyncControllerTest.PauseResume (812 ms)
...
[==========] 35 tests from 2 test suites ran. (4238 ms total)
[  PASSED  ] 35 tests.
```

### 失败输出示例

```
[ RUN      ] ThreadSafeQueueTest.ConcurrentPushPop
d:\code\zenplay\tests\test_thread_safe_queue.cpp:145: Failure
Expected equality of these values:
  consumed_values[i]
    Which is: 42
  static_cast<int>(i)
    Which is: 0
[  FAILED  ] ThreadSafeQueueTest.ConcurrentPushPop (1523 ms)
```

---

## 🧪 添加新测试

### 1. 创建新测试文件

```cpp
// tests/test_my_component.cpp
#include <gtest/gtest.h>
#include "player/my_component.h"

TEST(MyComponentTest, BasicFunctionality) {
  MyComponent comp;
  EXPECT_EQ(comp.GetValue(), 42);
}
```

### 2. 更新 CMakeLists.txt

在 `tests/CMakeLists.txt` 中添加：

```cmake
set(TEST_SOURCES
    test_main.cpp
    test_thread_safe_queue.cpp
    test_av_sync_controller.cpp
    test_my_component.cpp  # 新增
)
```

### 3. 重新构建并运行

```powershell
cmake --build build --config Debug
.\build\tests\Debug\zenplay_tests.exe
```

---

## 📈 性能基准测试

### ThreadSafeQueue 基准

**测试场景**：1,000,000 个元素的生产-消费

**预期结果**（任务 4 验收标准）：
- 使用条件变量：CPU 占用 < 3%
- 对比 sleep 轮询：CPU 占用降低 5-15%

**运行方式**：

```powershell
# 运行基准测试
.\build\tests\Debug\zenplay_tests.exe --gtest_also_run_disabled_tests --gtest_filter=ThreadSafeQueueTest.PerformanceBenchmark

# 同时使用 top/htop/Process Explorer 监控 CPU 占用
```

### AVSyncController 基准

**测试场景**：10,000 帧的高频更新（模拟 60fps）

**预期结果**（任务 3 验收标准）：
- 每帧处理时间 < 1ms
- 平均同步误差 < 30ms
- 最大同步误差 < 100ms

**运行方式**：

```powershell
.\build\tests\Debug\zenplay_tests.exe --gtest_also_run_disabled_tests --gtest_filter=AVSyncControllerTest.PerformanceStressTest
```

---

## 🐛 调试测试失败

### 启用详细输出

```powershell
# GTest 详细输出
.\build\tests\Debug\zenplay_tests.exe --gtest_verbose

# CTest 详细输出
ctest --output-on-failure --verbose --config Debug
```

### 使用 Visual Studio 调试

1. 在 Visual Studio 中打开 `build/zenplay.sln`
2. 右键 `zenplay_tests` 项目 → 设为启动项目
3. 设置断点并按 F5 调试

### 查看日志

测试运行时的日志（如果使用 LogManager）会输出到控制台或日志文件。

---

## ✅ CI/CD 集成

### GitHub Actions 示例

```yaml
name: Unit Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: windows-latest
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install Conan
        run: pip install conan
      
      - name: Install Dependencies
        run: conan install . --output-folder=build --build=missing
      
      - name: Configure CMake
        run: cmake -B build -DBUILD_TESTING=ON
      
      - name: Build
        run: cmake --build build --config Debug
      
      - name: Run Tests
        run: |
          cd build
          ctest --output-on-failure --config Debug
```

---

## 📚 参考资料

- [Google Test 文档](https://google.github.io/googletest/)
- [CMake CTest 文档](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
- [execution_plan_priority_features.md](../docs/execution_plan_priority_features.md) - 优化项详细设计

---

## 🎯 下一步

根据 execution_plan_priority_features.md，后续可添加的测试：

1. **任务 2** - 统一错误处理
   - `test_result.cpp`：测试 `Result<T>` 模板
   - `test_error_code.cpp`：测试 `ErrorCode` 传播

2. **任务 5** - 音频重采样优化
   - `test_audio_resampler.cpp`：测试重采样性能与正确性
   - `test_audio_player.cpp`：测试解码线程重采样

3. **任务 6** - 硬件加速
   - `test_decoder_factory.cpp`：测试硬件解码器选择
   - `test_hw_decoder.cpp`：测试 D3D11VA/NVDEC 解码器

4. **任务 7** - 性能监控
   - `test_statistics_manager.cpp`：测试统计数据收集
   - `benchmark_playback.cpp`：端到端播放性能测试

---

**最后更新**：2024-12-19  
**维护者**：Zenplay Team
