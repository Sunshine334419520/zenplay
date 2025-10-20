# ZenPlay 项目质量保障体系

> 目标：建立专业级的质量保障基础设施，确保代码质量、性能可追溯、问题可诊断

创建日期：2024-12-19  
最后更新：2024-12-19

---

## 📋 目录

- [一、质量保障全景图](#一质量保障全景图)
- [二、单元测试体系（GoogleTest）](#二单元测试体系googletest)
- [三、性能分析体系（Tracy Profiler）](#三性能分析体系tracy-profiler)
- [四、代码质量工具](#四代码质量工具)
- [五、日志与监控增强](#五日志与监控增强)
- [六、持续集成（CI/CD）](#六持续集成cicd)
- [七、文档与知识管理](#七文档与知识管理)
- [八、实施路线图](#八实施路线图)

---

## 一、质量保障全景图

### 专业项目的质量保障体系分层

```
┌─────────────────────────────────────────────────────────┐
│                   开发阶段保障                           │
├─────────────────────────────────────────────────────────┤
│ 1. 静态分析       │ clang-tidy, cppcheck, sanitizers   │
│ 2. 代码规范       │ clang-format, .editorconfig        │
│ 3. 单元测试       │ GoogleTest + 覆盖率 (gcov/lcov)    │
│ 4. 集成测试       │ 端到端播放测试                      │
├─────────────────────────────────────────────────────────┤
│                   运行时保障                             │
├─────────────────────────────────────────────────────────┤
│ 5. 性能分析       │ Tracy Profiler (CPU/GPU/Memory)    │
│ 6. 日志系统       │ spdlog (结构化日志 + 级别控制)      │
│ 7. 统计监控       │ StatisticsManager (实时指标)       │
│ 8. 崩溃报告       │ Breakpad/Crashpad (符号化)         │
├─────────────────────────────────────────────────────────┤
│                   持续集成保障                           │
├─────────────────────────────────────────────────────────┤
│ 9. CI/CD         │ GitHub Actions (自动构建/测试)      │
│ 10. 基准测试      │ Google Benchmark (性能回归检测)    │
│ 11. 内存检测      │ Valgrind/ASan/MSan                 │
├─────────────────────────────────────────────────────────┤
│                   发布后保障                             │
├─────────────────────────────────────────────────────────┤
│ 12. 用户反馈      │ 崩溃报告收集 + 日志上传             │
│ 13. 性能遥测      │ 匿名使用统计（可选）                │
└─────────────────────────────────────────────────────────┘
```

### 当前 ZenPlay 已有 vs 缺失

| 类别 | 已有 ✅ | 缺失 ❌ |
|------|---------|---------|
| **日志** | spdlog 集成 | 结构化日志、远程上传 |
| **监控** | StatisticsManager | 实时可视化、性能基线 |
| **测试** | - | 单元测试、集成测试、覆盖率 |
| **性能** | Timer 工具 | Tracy、GPU 分析、内存分析 |
| **代码质量** | - | clang-tidy、sanitizers、格式化 |
| **CI/CD** | - | 自动构建、测试、发布 |
| **崩溃报告** | - | Breakpad/Crashpad |

---

## 二、单元测试体系（GoogleTest）

### 2.1 为什么需要单元测试？

**专业项目必须有单元测试的原因**：
1. **回归检测**：修改代码后快速验证是否破坏原有功能
2. **设计质量**：可测试的代码通常设计更好（低耦合、高内聚）
3. **文档作用**：测试用例即活文档，展示如何使用 API
4. **重构信心**：有测试保护，敢于大胆优化代码
5. **Bug 复现**：用测试用例复现 Bug，修复后永久防止回归

### 2.2 GoogleTest 集成方案

#### Step 1：在 CMakeLists.txt 中启用测试

```cmake
# CMakeLists.txt（顶层）

cmake_minimum_required(VERSION 3.23)
project(zenplay)

# 选项：是否构建测试（默认开启）
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)

# ... 现有配置 ...

# 启用测试
if(BUILD_TESTS)
  enable_testing()
  
  # 引入 GoogleTest
  find_package(GTest REQUIRED)
  
  # 添加测试子目录
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  find_package(benchmark REQUIRED)
  add_subdirectory(benchmarks)
endif()
```

#### Step 2：创建测试目录结构

```
tests/
├── CMakeLists.txt                  # 测试构建配置
├── test_main.cpp                   # 测试入口（gtest main）
├── common/
│   ├── thread_safe_queue_test.cpp  # 队列测试
│   ├── timer_test.cpp              # 计时器测试
│   └── player_state_manager_test.cpp
├── sync/
│   ├── av_sync_controller_test.cpp # 同步控制器测试
│   └── clock_drift_test.cpp
├── audio/
│   ├── audio_player_test.cpp
│   └── resampler_test.cpp
├── video/
│   ├── video_player_test.cpp
│   └── renderer_test.cpp
├── codec/
│   ├── decoder_test.cpp
│   └── hw_decoder_test.cpp
└── integration/
    ├── playback_test.cpp           # 端到端播放测试
    └── seek_test.cpp
```

#### Step 3：测试 CMakeLists.txt

```cmake
# tests/CMakeLists.txt

# 收集所有测试源文件
file(GLOB_RECURSE TEST_SOURCES
  "common/*.cpp"
  "sync/*.cpp"
  "audio/*.cpp"
  "video/*.cpp"
  "codec/*.cpp"
  "integration/*.cpp"
)

# 创建测试可执行文件
add_executable(zenplay_tests
  test_main.cpp
  ${TEST_SOURCES}
)

# 链接库
target_link_libraries(zenplay_tests
  PRIVATE
    GTest::gtest
    GTest::gtest_main
    # 链接到主项目的库（需要将主项目改为库）
    zenplay_core
)

# 包含主项目头文件
target_include_directories(zenplay_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# 注册测试到 CTest
include(GoogleTest)
gtest_discover_tests(zenplay_tests)
```

#### Step 4：重构主项目为库 + 可执行文件

```cmake
# 顶层 CMakeLists.txt（修改）

# 将核心代码编译为库（供测试链接）
file(GLOB_RECURSE CORE_SOURCES
  "src/player/*.cpp"
  "src/player/*.h"
  # 排除 main.cpp
)
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

add_library(zenplay_core STATIC ${CORE_SOURCES})

# 主可执行文件
add_executable(zenplay src/main.cpp)
target_link_libraries(zenplay PRIVATE zenplay_core)

# 测试可执行文件
if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
```

### 2.3 编写测试示例

#### 示例 1：测试 `ThreadSafeQueue`

```cpp
// tests/common/thread_safe_queue_test.cpp

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "player/common/thread_safe_queue.h"

class ThreadSafeQueueTest : public ::testing::Test {
 protected:
  ThreadSafeQueue<int> queue_;
};

TEST_F(ThreadSafeQueueTest, PushAndPop) {
  queue_.Push(42);
  
  int value;
  ASSERT_TRUE(queue_.Pop(value, std::chrono::milliseconds(100)));
  EXPECT_EQ(value, 42);
}

TEST_F(ThreadSafeQueueTest, PopTimeout) {
  int value;
  ASSERT_FALSE(queue_.Pop(value, std::chrono::milliseconds(50)));
}

TEST_F(ThreadSafeQueueTest, MultiThreadedProducerConsumer) {
  const int kNumItems = 1000;
  
  // 生产者线程
  std::thread producer([this]() {
    for (int i = 0; i < kNumItems; ++i) {
      queue_.Push(i);
    }
  });
  
  // 消费者线程
  std::vector<int> consumed;
  std::thread consumer([this, &consumed]() {
    for (int i = 0; i < kNumItems; ++i) {
      int value;
      if (queue_.Pop(value, std::chrono::seconds(1))) {
        consumed.push_back(value);
      }
    }
  });
  
  producer.join();
  consumer.join();
  
  ASSERT_EQ(consumed.size(), kNumItems);
  
  // 验证所有数据都被消费（不关心顺序）
  std::sort(consumed.begin(), consumed.end());
  for (int i = 0; i < kNumItems; ++i) {
    EXPECT_EQ(consumed[i], i);
  }
}

TEST_F(ThreadSafeQueueTest, ClearWhileBlocking) {
  // 启动阻塞的 Pop 线程
  std::atomic<bool> pop_returned{false};
  std::thread popper([this, &pop_returned]() {
    int value;
    queue_.Pop(value, std::chrono::seconds(10));
    pop_returned = true;
  });
  
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // 清空队列应该唤醒等待线程
  queue_.Stop();
  
  popper.join();
  EXPECT_TRUE(pop_returned);
}
```

#### 示例 2：测试 `AVSyncController`

```cpp
// tests/sync/av_sync_controller_test.cpp

#include <gtest/gtest.h>
#include "player/sync/av_sync_controller.h"

class AVSyncControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sync_controller_ = std::make_unique<AVSyncController>();
  }

  std::unique_ptr<AVSyncController> sync_controller_;
};

TEST_F(AVSyncControllerTest, InitialState) {
  EXPECT_EQ(sync_controller_->GetSyncMode(), 
            AVSyncController::SyncMode::AUDIO_MASTER);
  
  auto now = std::chrono::steady_clock::now();
  EXPECT_DOUBLE_EQ(sync_controller_->GetMasterClock(now), 0.0);
}

TEST_F(AVSyncControllerTest, AudioClockUpdate) {
  auto now = std::chrono::steady_clock::now();
  
  // 第一次更新（归一化为 0）
  sync_controller_->UpdateAudioClock(1000.0, now);
  
  double clock = sync_controller_->GetMasterClock(now);
  EXPECT_NEAR(clock, 0.0, 10.0);  // 第一帧归一化为 0
  
  // 第二次更新（100ms 后）
  auto later = now + std::chrono::milliseconds(100);
  sync_controller_->UpdateAudioClock(1100.0, later);
  
  clock = sync_controller_->GetMasterClock(later);
  EXPECT_NEAR(clock, 100.0, 20.0);  // 应该约为 100ms
}

TEST_F(AVSyncControllerTest, PauseResume) {
  auto start = std::chrono::steady_clock::now();
  
  sync_controller_->UpdateAudioClock(1000.0, start);
  
  // 暂停
  auto pause_time = start + std::chrono::milliseconds(500);
  sync_controller_->Pause();
  
  // 暂停期间时钟不应前进
  auto during_pause = pause_time + std::chrono::milliseconds(500);
  double clock_during = sync_controller_->GetMasterClock(during_pause);
  
  // 恢复
  auto resume_time = during_pause + std::chrono::milliseconds(100);
  sync_controller_->Resume();
  
  auto after_resume = resume_time + std::chrono::milliseconds(100);
  double clock_after = sync_controller_->GetMasterClock(after_resume);
  
  // 暂停期间时钟应该冻结
  EXPECT_NEAR(clock_during, clock_after, 150.0);
}

TEST_F(AVSyncControllerTest, SeekReset) {
  auto now = std::chrono::steady_clock::now();
  
  // 播放到 5 秒
  sync_controller_->UpdateAudioClock(5000.0, now);
  
  // Seek 到 10 秒
  sync_controller_->ResetForSeek(10000);
  
  // 时钟应该被重置
  double clock = sync_controller_->GetMasterClock(now);
  EXPECT_NEAR(clock, 0.0, 10.0);  // 重置后归零
}

TEST_F(AVSyncControllerTest, ClockDriftSmoothing) {
  auto now = std::chrono::steady_clock::now();
  
  // 模拟抖动的时钟更新
  sync_controller_->UpdateAudioClock(1000.0, now);
  
  auto t1 = now + std::chrono::milliseconds(100);
  sync_controller_->UpdateAudioClock(1100.0, t1);
  double clock1 = sync_controller_->GetMasterClock(t1);
  
  auto t2 = t1 + std::chrono::milliseconds(100);
  sync_controller_->UpdateAudioClock(1250.0, t2);  // 跳变 50ms
  double clock2 = sync_controller_->GetMasterClock(t2);
  
  // 平滑后的时钟不应该跳变太大
  EXPECT_LT(std::abs(clock2 - clock1 - 100.0), 30.0);
}
```

#### 示例 3：Mock 测试（隔离依赖）

```cpp
// tests/audio/audio_player_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "player/audio/audio_player.h"

using ::testing::_;
using ::testing::Return;

// Mock AudioOutput
class MockAudioOutput : public AudioOutput {
 public:
  MOCK_METHOD(bool, Init, (const AudioSpec&, AudioOutputCallback, void*), (override));
  MOCK_METHOD(bool, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(void, Pause, (), (override));
  MOCK_METHOD(void, Resume, (), (override));
  MOCK_METHOD(void, SetVolume, (float), (override));
  MOCK_METHOD(float, GetVolume, (), (const, override));
  MOCK_METHOD(void, Flush, (), (override));
  MOCK_METHOD(void, Cleanup, (), (override));
};

class AudioPlayerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_audio_output_ = std::make_unique<MockAudioOutput>();
    state_manager_ = std::make_unique<PlayerStateManager>();
    sync_controller_ = std::make_unique<AVSyncController>();
    
    audio_player_ = std::make_unique<AudioPlayer>(
        state_manager_.get(), sync_controller_.get());
  }

  std::unique_ptr<MockAudioOutput> mock_audio_output_;
  std::unique_ptr<PlayerStateManager> state_manager_;
  std::unique_ptr<AVSyncController> sync_controller_;
  std::unique_ptr<AudioPlayer> audio_player_;
};

TEST_F(AudioPlayerTest, InitSuccess) {
  EXPECT_CALL(*mock_audio_output_, Init(_, _, _))
      .WillOnce(Return(true));
  
  AudioPlayer::AudioConfig config;
  EXPECT_TRUE(audio_player_->Init(config));
}

TEST_F(AudioPlayerTest, StartFailsWhenNotInitialized) {
  // 未初始化时启动应该失败
  EXPECT_FALSE(audio_player_->Start());
}
```

### 2.4 运行测试

```bash
# 构建测试
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build . --target zenplay_tests

# 运行所有测试
ctest --output-on-failure

# 或直接运行测试可执行文件
./tests/zenplay_tests

# 运行特定测试
./tests/zenplay_tests --gtest_filter=AVSyncControllerTest.*

# 生成 XML 报告（用于 CI）
./tests/zenplay_tests --gtest_output=xml:test_results.xml
```

### 2.5 代码覆盖率

```cmake
# CMakeLists.txt（添加覆盖率支持）

option(ENABLE_COVERAGE "Enable code coverage" OFF)

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(zenplay_core PRIVATE --coverage)
  target_link_options(zenplay_core PRIVATE --coverage)
endif()
```

```bash
# 生成覆盖率报告
cmake .. -DENABLE_COVERAGE=ON
make
make test

# 使用 lcov 生成 HTML 报告
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' --output-file coverage.info
lcov --remove coverage.info '*/third_party/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_html

# 打开报告
firefox coverage_html/index.html
```

**目标覆盖率**：
- 核心逻辑（AVSyncController、ThreadSafeQueue）：**>= 90%**
- 工具类（Timer、Logger）：**>= 80%**
- UI 代码：**>= 50%**（UI 难测试）

---

## 三、性能分析体系（Tracy Profiler）

### 3.1 Tracy 是什么？为什么选择它？

**Tracy Profiler** 是现代 C++ 项目的实时性能分析工具，特点：
- ✅ **实时可视化**：运行时查看 CPU 时间线、函数耗时
- ✅ **低开销**：生产环境可用（< 1% 性能影响）
- ✅ **GPU 支持**：可分析 OpenGL/Vulkan/D3D11 性能
- ✅ **内存追踪**：检测内存泄漏、分配热点
- ✅ **跨平台**：Windows/Linux/macOS
- ✅ **易集成**：单头文件或 CMake 集成

### 3.2 Tracy 能解决什么问题？

#### 问题 1：不知道性能瓶颈在哪

**场景**：视频播放卡顿，但不知道是解码慢、渲染慢还是同步问题。

**Tracy 解决方案**：
```cpp
// 在关键函数添加 Zone 标记
void PlaybackController::VideoDecodeTask() {
  ZoneScoped;  // ✅ Tracy 自动记录此函数耗时
  
  while (running_) {
    {
      ZoneScopedN("Decode Frame");  // ✅ 命名子区域
      decoder->Decode(...);
    }
    
    {
      ZoneScopedN("Push to Queue");
      video_queue_.Push(...);
    }
  }
}
```

**Tracy UI 显示**：
```
Timeline:
├── VideoDecodeTask [12.5ms]
│   ├── Decode Frame [10.2ms]  ← 瓶颈！
│   └── Push to Queue [0.3ms]
├── AudioDecodeTask [2.1ms]
└── RenderThread [4.5ms]
```

#### 问题 2：不知道函数被调用多少次

**场景**：怀疑 `swr_convert` 被频繁调用导致 CPU 高。

**Tracy 解决方案**：
```cpp
int AudioPlayer::FillAudioBuffer(...) {
  ZoneScoped;
  // Tracy 自动统计调用次数、平均耗时、最大耗时
}
```

**Tracy 统计**：
```
FillAudioBuffer:
  Calls: 1,234 次
  Avg: 2.3ms
  Max: 15.7ms  ← 发现异常调用
  Total: 2,839ms (占总时间 18%)
```

#### 问题 3：内存泄漏难定位

**Tracy 解决方案**：
```cpp
// 启用内存追踪
#define TRACY_ENABLE
#include <tracy/Tracy.hpp>

void* operator new(size_t size) {
  void* ptr = malloc(size);
  TracyAlloc(ptr, size);  // ✅ 追踪分配
  return ptr;
}

void operator delete(void* ptr) noexcept {
  TracyFree(ptr);  // ✅ 追踪释放
  free(ptr);
}
```

**Tracy 内存视图**：
- 显示每个分配点的调用栈
- 实时内存占用曲线
- 泄漏的内存块高亮

### 3.3 Tracy 集成步骤

#### Step 1：添加 Tracy 到项目

```cmake
# CMakeLists.txt

option(ENABLE_TRACY "Enable Tracy profiler" OFF)

if(ENABLE_TRACY)
  include(FetchContent)
  
  FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.10
  )
  
  FetchContent_MakeAvailable(tracy)
  
  # 添加 Tracy 客户端库
  add_library(TracyClient STATIC)
  target_sources(TracyClient PRIVATE
    ${tracy_SOURCE_DIR}/public/TracyClient.cpp
  )
  target_include_directories(TracyClient PUBLIC
    ${tracy_SOURCE_DIR}/public
  )
  target_compile_definitions(TracyClient PUBLIC
    TRACY_ENABLE
  )
  
  # 链接到主项目
  target_link_libraries(zenplay_core PUBLIC TracyClient)
  target_compile_definitions(zenplay_core PUBLIC TRACY_ENABLE)
endif()
```

#### Step 2：在代码中添加标记

```cpp
// src/player/playback_controller.cpp

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void PlaybackController::DemuxLoop() {
#ifdef TRACY_ENABLE
  ZoneScoped;  // ✅ 函数级追踪
#endif

  while (running_) {
    AVPacket* packet = av_packet_alloc();
    
    {
#ifdef TRACY_ENABLE
      ZoneScopedN("Read Packet");  // ✅ 细粒度追踪
#endif
      demuxer_->ReadPacket(&packet);
    }
    
    // ...
  }
}

void PlaybackController::VideoDecodeTask() {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif

  while (running_) {
    TIMER_START(video_decode);  // 现有 Timer
    
#ifdef TRACY_ENABLE
    ZoneScopedN("Decode Video Frame");  // Tracy 追踪
#endif
    
    decoder->Decode(...);
    
    auto decode_time_ms = TIMER_END_MS(video_decode);
    
#ifdef TRACY_ENABLE
    TracyPlot("Video Decode Time (ms)", decode_time_ms);  // ✅ 绘制曲线
#endif
  }
}
```

#### Step 3：运行 Tracy Server

```bash
# 下载 Tracy Server（GUI 工具）
# https://github.com/wolfpld/tracy/releases

# Linux
wget https://github.com/wolfpld/tracy/releases/download/v0.10/Tracy-0.10.tar.gz
tar xzf Tracy-0.10.tar.gz
cd Tracy-0.10
./Tracy

# Windows
# 下载 Tracy.exe 并运行

# macOS
brew install tracy
tracy
```

#### Step 4：运行程序并连接

```bash
# 构建启用 Tracy 的版本
cmake .. -DENABLE_TRACY=ON
cmake --build .

# 运行程序（会自动连接到 Tracy Server）
./zenplay

# Tracy Server 会显示：
# "Connected to zenplay (127.0.0.1)"
```

### 3.4 Tracy 高级功能

#### 功能 1：帧标记

```cpp
// 在主循环标记帧边界
void MainLoop() {
  while (running_) {
    FrameMark;  // ✅ Tracy 记录帧边界
    
    ProcessFrame();
    Render();
  }
}
```

**Tracy 显示**：
- 帧率曲线（实时 FPS）
- 每帧耗时分布
- 帧间隔抖动分析

#### 功能 2：自定义绘图

```cpp
// 绘制性能曲线
TracyPlot("Audio Queue Size", audio_queue_.Size());
TracyPlot("Video Queue Size", video_queue_.Size());
TracyPlot("Sync Offset (ms)", sync_controller_->GetSyncOffset());
TracyPlot("CPU Usage (%)", GetCPUUsage());
```

**Tracy 显示**：多条曲线叠加，方便对比分析。

#### 功能 3：消息日志

```cpp
TracyMessage("Seek started", 12);
TracyMessageC("Error: decoder failed", 18, 0xFF0000);  // 红色消息
```

**Tracy 显示**：在时间线上标记事件点。

#### 功能 4：锁竞争分析

```cpp
// 替换 std::mutex
TracyLockable(std::mutex, my_mutex_);

void SomeFunction() {
  std::lock_guard<LockableBase(std::mutex)> lock(my_mutex_);
  // Tracy 记录锁等待时间
}
```

**Tracy 显示**：
- 锁等待时间
- 锁持有时间
- 死锁检测

### 3.5 Tracy 与现有工具对比

| 工具 | 优势 | 劣势 | 适用场景 |
|------|------|------|---------|
| **Tracy** | 实时可视化、低开销、GPU 支持 | 需要额外工具 | 日常开发、性能调优 |
| **Timer 宏** | 零依赖、简单 | 只有数字、无可视化 | 快速定位、CI 基准 |
| **perf** | 系统级分析、无需修改代码 | Linux only、学习曲线陡 | 线上问题诊断 |
| **VTune** | Intel 官方、功能强大 | 商业软件、重量级 | 专业性能优化 |

**建议**：
- **日常开发**：Tracy（实时反馈）
- **CI 基准**：Timer + Google Benchmark
- **线上诊断**：日志 + StatisticsManager

---

## 四、代码质量工具

### 4.1 静态分析（clang-tidy）

**作用**：在编译前检测潜在问题（内存泄漏、未初始化变量、性能问题）。

```cmake
# CMakeLists.txt

option(ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)

if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY 
      ${CLANG_TIDY_EXE};
      -checks=-*,modernize-*,performance-*,bugprone-*,readability-*;
      -header-filter=.*src/.*;
    )
  endif()
endif()
```

```yaml
# .clang-tidy

Checks: >
  -*,
  bugprone-*,
  modernize-*,
  performance-*,
  readability-*,
  -modernize-use-trailing-return-type

CheckOptions:
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
  - key: readability-identifier-naming.FunctionCase
    value: CamelCase
  - key: readability-identifier-naming.VariableCase
    value: lower_case
  - key: readability-identifier-naming.PrivateMemberSuffix
    value: '_'
```

### 4.2 内存检测（Sanitizers）

```cmake
# CMakeLists.txt

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN)
  target_compile_options(zenplay_core PRIVATE -fsanitize=address)
  target_link_options(zenplay_core PRIVATE -fsanitize=address)
endif()

if(ENABLE_UBSAN)
  target_compile_options(zenplay_core PRIVATE -fsanitize=undefined)
  target_link_options(zenplay_core PRIVATE -fsanitize=undefined)
endif()

if(ENABLE_TSAN)
  target_compile_options(zenplay_core PRIVATE -fsanitize=thread)
  target_link_options(zenplay_core PRIVATE -fsanitize=thread)
endif()
```

**运行示例**：
```bash
# AddressSanitizer（检测内存泄漏、越界）
cmake .. -DENABLE_ASAN=ON
make && ./zenplay

# ThreadSanitizer（检测数据竞争、死锁）
cmake .. -DENABLE_TSAN=ON
make && ./zenplay
```

### 4.3 代码格式化（clang-format）

```yaml
# .clang-format

BasedOnStyle: Google
IndentWidth: 2
ColumnLimit: 80
PointerAlignment: Left
AccessModifierOffset: -1
AllowShortFunctionsOnASingleLine: Inline
```

```bash
# 格式化所有代码
find src -name "*.cpp" -o -name "*.h" | xargs clang-format -i

# 检查格式（CI 使用）
find src -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

---

## 五、日志与监控增强

### 5.1 结构化日志

**当前问题**：日志是纯文本，难以自动分析。

**改进方案**：输出 JSON 格式日志。

```cpp
// src/player/common/log_manager.cpp

void LogManager::LogStructured(const std::string& module,
                               LogLevel level,
                               const std::string& message,
                               const nlohmann::json& context) {
  nlohmann::json log_entry;
  log_entry["timestamp"] = GetCurrentTimestamp();
  log_entry["level"] = LogLevelToString(level);
  log_entry["module"] = module;
  log_entry["message"] = message;
  log_entry["context"] = context;
  log_entry["thread_id"] = std::this_thread::get_id();
  
  // 输出到文件
  structured_logger_->info(log_entry.dump());
}

// 使用示例
nlohmann::json ctx;
ctx["file_path"] = "/path/to/video.mp4";
ctx["duration_ms"] = 120000;
ctx["video_codec"] = "h264";

MODULE_STRUCTURED_LOG(LOG_MODULE_DEMUX, LogLevel::INFO, 
                     "File opened successfully", ctx);
```

### 5.2 实时监控仪表盘（可选）

使用 Prometheus + Grafana 或简单的 Web 界面：

```cpp
// src/player/stats/stats_exporter.h

class StatsExporter {
 public:
  void StartHttpServer(int port = 9090);
  
  // HTTP 端点：http://localhost:9090/metrics
  std::string GetMetricsJson();
  
 private:
  std::unique_ptr<httplib::Server> server_;
};
```

**示例输出** (http://localhost:9090/metrics)：
```json
{
  "timestamp": "2024-12-19T10:30:45Z",
  "fps": 29.97,
  "cpu_percent": 15.2,
  "memory_mb": 234,
  "video_queue_size": 12,
  "audio_queue_size": 8,
  "sync_offset_ms": 23.5,
  "dropped_frames": 3
}
```

---

## 六、持续集成（CI/CD）

### 6.1 GitHub Actions 配置

```yaml
# .github/workflows/ci.yml

name: CI

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
        build_type: [Debug, Release]
    
    runs-on: ${{ matrix.os }}
    
    steps:
    - uses: actions/checkout@v3
    
    - name: Install dependencies (Ubuntu)
      if: runner.os == 'Linux'
      run: |
        sudo apt-get update
        sudo apt-get install -y libsdl2-dev libasound2-dev
    
    - name: Setup Conan
      run: |
        pip install conan
        conan profile detect --force
    
    - name: Install dependencies
      run: |
        conan install . --build=missing -s build_type=${{ matrix.build_type }}
    
    - name: Configure CMake
      run: |
        cmake --preset conan-default \
          -DBUILD_TESTS=ON \
          -DENABLE_COVERAGE=${{ matrix.build_type == 'Debug' }}
    
    - name: Build
      run: cmake --build build/${{ matrix.build_type }}
    
    - name: Run tests
      run: |
        cd build/${{ matrix.build_type }}
        ctest --output-on-failure
    
    - name: Generate coverage (Debug only)
      if: matrix.build_type == 'Debug' && runner.os == 'Linux'
      run: |
        lcov --capture --directory . --output-file coverage.info
        lcov --remove coverage.info '/usr/*' '*/third_party/*' --output-file coverage.info
    
    - name: Upload coverage to Codecov
      if: matrix.build_type == 'Debug' && runner.os == 'Linux'
      uses: codecov/codecov-action@v3
      with:
        file: ./coverage.info
```

### 6.2 性能回归检测

```yaml
# .github/workflows/benchmark.yml

name: Benchmark

on:
  pull_request:
    branches: [ main ]

jobs:
  benchmark:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v3
    
    - name: Build benchmarks
      run: |
        cmake .. -DBUILD_BENCHMARKS=ON
        make
    
    - name: Run benchmarks
      run: |
        ./benchmarks/zenplay_benchmarks --benchmark_out=bench_results.json
    
    - name: Compare with baseline
      run: |
        python scripts/compare_benchmark.py \
          bench_results.json \
          baseline_bench_results.json
```

---

## 七、文档与知识管理

### 7.1 API 文档（Doxygen）

```cpp
/**
 * @brief 音视频同步控制器
 * 
 * 负责管理音频和视频的时钟同步，实现以下同步策略：
 * 1. 以音频为主时钟（音频播放更稳定）
 * 2. 视频根据音频时钟调整显示时间
 * 3. 处理时钟漂移和同步误差
 * 
 * @example
 * @code
 * AVSyncController sync;
 * sync.SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
 * sync.UpdateAudioClock(1000.0, std::chrono::steady_clock::now());
 * @endcode
 */
class AVSyncController {
 public:
  /**
   * @brief 更新音频时钟
   * 
   * @param audio_pts_ms 音频 PTS（毫秒）
   * @param system_time 系统时间戳
   * 
   * @note 调用频率：约每 50-100ms 更新一次
   * @thread_safety 线程安全
   */
  void UpdateAudioClock(int64_t audio_pts_ms,
                       std::chrono::steady_clock::time_point system_time);
};
```

### 7.2 架构决策记录（ADR）

```markdown
# ADR-001: 选择音频主时钟同步模式

**状态**: 已接受  
**日期**: 2024-12-19

## 背景

需要选择音视频同步的主时钟来源。

## 决策

使用音频主时钟（AUDIO_MASTER）。

## 理由

1. 音频硬件时钟稳定
2. 音频卡顿用户体验差
3. 视频可以通过丢帧/重复帧适应

## 后果

- 需要视频渲染支持丢帧
- 音频输出必须稳定（低延迟）

## 替代方案

- 外部时钟：仅适用于无音频场景
- 视频主时钟：会导致音频卡顿
```

---

## 八、实施路线图

### Phase 1：测试基础设施（Week 1-2）

```
优先级 P0
├── GoogleTest 集成
├── 核心模块单元测试（Queue/Timer/Sync）
└── CI 自动化测试
```

**验收标准**：
- ✅ 至少 20 个单元测试通过
- ✅ CI 自动运行测试
- ✅ 核心模块覆盖率 > 70%

### Phase 2：性能分析工具（Week 2-3）

```
优先级 P1
├── Tracy 集成
├── 关键路径添加 Zone 标记
└── 性能基线建立
```

**验收标准**：
- ✅ Tracy 可视化主要线程
- ✅ 建立性能基线数据
- ✅ 识别 Top 3 性能瓶颈

### Phase 3：代码质量工具（Week 3-4）

```
优先级 P1
├── clang-tidy 集成
├── clang-format 配置
└── Sanitizers 集成
```

**验收标准**：
- ✅ clang-tidy 检查通过
- ✅ ASan/TSan 无错误
- ✅ 代码格式统一

### Phase 4：监控增强（Week 4-5）

```
优先级 P2
├── 结构化日志
├── 实时监控仪表盘（可选）
└── 崩溃报告集成（可选）
```

---

## 九、总结对比表

| 维度 | 当前状态 | 完善后 | 收益 |
|------|---------|--------|------|
| **测试** | 无 | 单元测试 + 集成测试 + 覆盖率 | 回归检测、重构信心 ↑↑↑ |
| **性能分析** | Timer 宏 | Tracy 实时可视化 | 瓶颈定位效率 ↑↑↑ |
| **代码质量** | 人工 Review | clang-tidy + Sanitizers | Bug 提前发现 ↑↑ |
| **日志** | spdlog 文本 | 结构化 JSON + 级别控制 | 问题诊断效率 ↑↑ |
| **监控** | StatisticsManager | 实时仪表盘 + 曲线 | 运行时可观测性 ↑↑ |
| **CI/CD** | 无 | GitHub Actions 自动化 | 质量保障 ↑↑↑ |

**专业项目 vs 个人项目的差距**：
- 个人项目：能跑就行
- 专业项目：可测试、可观测、可维护、可扩展

---

## 附录：快速启动命令

```bash
# 1. 启用测试
cmake .. -DBUILD_TESTS=ON
make test

# 2. 启用 Tracy
cmake .. -DENABLE_TRACY=ON
make
./zenplay  # 然后启动 Tracy Server

# 3. 启用 sanitizers
cmake .. -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
make
./zenplay

# 4. 代码覆盖率
cmake .. -DENABLE_COVERAGE=ON
make test
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html

# 5. 格式化代码
find src -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

---

*文档维护者: ZenPlay 质量保障团队*  
*最后更新: 2024-12-19*
