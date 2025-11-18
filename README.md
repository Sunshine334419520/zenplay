# 🎬 ZenPlay - 从零构建的跨平台音视频播放器

<div align="center">

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Qt](https://img.shields.io/badge/Qt-6.7.3-green.svg)
![FFmpeg](https://img.shields.io/badge/FFmpeg-7.1.1-orange.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)

*一个从零开始编写的现代化多媒体播放器，专注于学习音视频技术和渲染技术*

[快速开始](#-快速开始) • [功能特性](#-功能特性) • [技术架构](#-技术架构) • [文档](#-技术文档)

</div>

---

## 📖 目录

- [项目初衷](#-项目初衷)
- [项目简介](#-项目简介)
- [功能特性](#-功能特性)
- [技术栈](#-技术栈)
- [项目结构](#-项目结构)
- [快速开始](#-快速开始)
  - [环境要求](#环境要求)
  - [构建步骤](#构建步骤)
- [技术文档](#-技术文档)
  - [核心技术](#核心技术)
  - [架构设计](#架构设计)
  - [组件详解](#组件详解)
- [开发路线](#-开发路线)
- [贡献指南](#-贡献指南)
- [许可证](#-许可证)

---

## 🎯 项目初衷

ZenPlay 诞生于一个简单的想法：**通过从零构建一个完整的音视频播放器，深入学习和掌握音视频技术与渲染技术**。

### 为什么要从零开始？

在音视频和多媒体领域，大多数开发者只是调用现成的库和框架，很少有机会深入理解底层原理。这个项目的目标是：

- 🔍 **深入理解**：通过自己实现每一个模块，真正理解音视频处理的每个环节
- 🎓 **系统学习**：涵盖解封装、解码、渲染、同步等完整的多媒体技术栈
- 💪 **工程实践**：学习如何设计和实现一个复杂的多线程、跨平台应用
- 📚 **知识沉淀**：将学习过程和技术细节文档化，便于回顾和分享

### 学习重点

- ✅ 音视频同步算法的原理和实现
- ✅ 多线程架构设计和线程安全
- ✅ 跨平台音频输出（WASAPI/ALSA）
- ✅ 硬件加速视频渲染（SDL2/OpenGL）
- ✅ FFmpeg 解封装和解码流程
- ✅ 实时性能监控和统计系统
- ✅ 现代 C++ 最佳实践（C++17）

这不仅仅是一个播放器，更是一个**学习音视频技术的完整实践项目**。

---

## 📝 项目简介

ZenPlay 是一个基于 **C++17** 开发的跨平台多媒体播放器，采用现代化的分层架构设计，支持本地文件和网络流媒体播放。

### 核心功能

- 🎥 **视频播放**：支持主流视频格式（MP4、AVI、MKV、FLV 等）
- 🎵 **音频播放**：支持多种音频编码（AAC、MP3、FLAC 等）
- 🎬 **流媒体支持**：支持 HTTP/RTSP/RTMP 等网络协议
- ⚡ **硬件加速**：利用 SDL2 实现高性能视频渲染
- 🎯 **精确同步**：专业级音视频同步算法，误差 < 50ms
- 🔄 **实时 Seek**：快速定位，支持前后跳转
- 📊 **性能监控**：实时统计解码帧率、缓冲区状态等

### 技术亮点

1. **分层架构**：UI 层、应用层、核心层、组件层、平台层清晰分离
2. **多线程设计**：解封装、音视频解码、渲染各自独立线程
3. **音视频对称**：AudioPlayer 和 VideoPlayer 地位平等的架构设计
4. **跨平台抽象**：通过接口实现音频输出和渲染的平台无关性
5. **现代 C++**：大量使用智能指针、RAII、原子操作等现代特性

---

## 🛠️ 技术栈

### 核心框架

| 技术 | 版本 | 用途 |
|------|------|------|
| **C++** | 17 | 开发语言 |
| **Qt** | 6.7.3 | UI 框架（Core, Widgets, Gui） |
| **FFmpeg** | 7.1.1 | 音视频解封装和解码 |
| **SDL2** | 2.32.2 | 视频渲染（硬件加速） |
| **spdlog** | 1.15.1 | 高性能日志库 |
| **nlohmann_json** | 3.12.0 | JSON 配置解析 |

### 平台特性

| 平台 | 音频输出 | 视频渲染 | 线程框架 |
|------|---------|---------|---------|
| **Windows** | WASAPI | SDL2/OpenGL | std::thread + Loki |
| **Linux** | ALSA | SDL2/OpenGL | std::thread + Loki |
| **macOS** | CoreAudio | SDL2/Metal | std::thread + Loki |

### 构建工具

- **构建系统**：CMake 3.23+
- **包管理**：Conan 2.x
- **编译器**：
  - Windows: MSVC 2019+ / MinGW
  - Linux: GCC 9+ / Clang 10+
  - macOS: Xcode 12+ / Clang 10+

---

## 🚀 快速开始

### 环境要求

#### 系统要求
- **操作系统**：Windows 10+, Ubuntu 20.04+, macOS 11+
- **RAM**：建议 4GB+
- **显卡**：支持 OpenGL 3.0+

#### 软件要求
- **CMake**：3.23 或更高版本
- **Conan**：2.0 或更高版本
- **编译器**：
  - Windows: Visual Studio 2019+ / MinGW-w64
  - Linux: GCC 9+ / Clang 10+
  - macOS: Xcode 12+ (Clang 10+)
- **Python**：3.7+ (用于 Conan)

### 构建步骤

#### 1. 安装 Conan

```bash
# 使用 pip 安装 Conan 2.x
pip install conan

# 验证安装
conan --version
```

#### 2. 配置 Conan 远程仓库

```bash
# 添加 Conan Center 仓库
conan remote add conancenter https://center.conan.io

# 如果已添加，可以更新
conan remote update conancenter https://center.conan.io
```

#### 3. 克隆项目

```bash
git clone https://github.com/Sunshine334419520/zenplay.git
cd zenplay
```

#### 4. 安装依赖

```bash
# 检测默认配置文件
conan profile detect --force

# 安装所有依赖（Debug 模式）
conan install . --build=missing -s build_type=Debug

# 或者 Release 模式
conan install . --build=missing -s build_type=Release
```

**注意**：首次构建需要编译 Qt6 和 FFmpeg，可能需要 30-60 分钟，请耐心等待。

#### 5. 配置和构建项目

##### Windows (Visual Studio)

```bash
# 配置项目
cmake --preset conan-default

# 构建
cmake --build --preset conan-debug
# 或者 Release 模式
# cmake --build --preset conan-release
```

##### Linux / macOS

```bash
# 配置项目
cmake --preset conan-default

# 构建
cmake --build build/Debug
# 或者 Release 模式
# cmake --build build/Release
```

#### 6. 运行程序

```bash
# Windows
./build/Debug/zenplay.exe

# Linux / macOS
./build/Debug/zenplay
```

## 📚 技术文档

本节提供 ZenPlay 项目的完整技术文档，从整体架构到具体实现细节。建议按顺序阅读以建立完整的技术理解。

---

### 1. 架构设计

**从宏观视角理解 ZenPlay 的整体设计和核心思想。**

- 📄 [**整体架构设计**](docs/architecture_overview.md) - **⭐ 必读：系统架构全景**
  - 五层架构设计：UI层 → 应用层 → 核心层 → 组件层 → 平台层
  - 核心组件关系图：ZenPlayer、PlaybackController、AudioPlayer、VideoPlayer
  - 数据流转全流程：从文件读取到屏幕显示
  - 多线程协作模型：5个线程的职责分工与通信机制
  - 设计原则与技术决策

- 📄 [**核心组件详解**](docs/core_components.md) - 关键模块深入解析
  - **ZenPlayer**：应用层统一接口，生命周期管理
  - **PlaybackController**：核心协调器，管理所有播放线程和队列
  - **AudioPlayer & VideoPlayer**：对称设计的音视频播放器
  - **AVSyncController**：音视频同步控制中心
  - **Demuxer & Decoders**：解封装与解码器架构

- 📄 [**状态管理系统**](docs/state_management.md) - 播放器状态机设计
  - PlayerStateManager 统一状态管理
  - 状态转换规则：Idle → Opening → Playing → Paused → Seeking → Stopped
  - 线程安全的状态同步
  - 观察者模式与状态通知
  - Seek 异步状态处理

---

### 2. 音视频同步

**播放器的核心技术，决定用户体验的关键。**

- 📄 [**音视频同步原理与实现**](docs/av_sync_design.md) - **⭐ 核心算法**
  - 三种同步模式：AUDIO_MASTER（推荐）、VIDEO_MASTER、EXTERNAL_MASTER
  - PTS/DTS 时间戳管理与计算
  - 主时钟选择策略：为什么选择音频时钟
  - 视频帧显示时机计算：如何决定何时显示下一帧
  - 音频时钟更新机制：从 AudioCallback 到 AVSyncController
  - 时钟漂移补偿算法：处理硬件时钟不准确的情况
  - 丢帧与重复帧策略：保持同步的关键技术

---

### 3. 渲染路径选择器

**智能选择最佳的硬件加速和渲染方案。**

- 📄 [**渲染路径选择器设计**](docs/render_path_selector.md) - 跨平台渲染决策
  - RenderPathSelector 架构：配置驱动的渲染器选择
  - 平台检测与能力探测：D3D11、DXVA2、VA-API、VideoToolbox
  - 硬件加速与软件渲染的选择策略
  - 自动降级机制：硬件失败时回退到软件渲染
  - 渲染器与硬件解码器的协同：共享 D3D11 设备实现零拷贝

---

### 4. 视频渲染

**高性能、低延迟的视频渲染实现。**

- 📄 [**视频渲染架构**](docs/video_rendering.md) - 完整的渲染流程
  - Renderer 接口抽象：跨平台渲染器统一接口
  - SDL2 渲染器实现：硬件加速纹理与 YUV 转换
  - D3D11 渲染器实现：Windows 原生 DirectX 渲染
  - RendererProxy 设计：线程安全的渲染代理
  - VideoPlayer 与 VideoRenderThread：独立渲染线程模型
  - 渲染性能优化：双缓冲、垂直同步、批量提交

- 📄 [**零拷贝渲染详解**](docs/zero_copy_rendering.md) - **⭐ 性能关键**
  - 零拷贝原理：GPU 内存直接访问，无 CPU 拷贝
  - 硬件解码与渲染的协同：共享 D3D11 设备
  - HWDecoderContext 与 D3D11Texture2D 的映射
  - 零拷贝验证机制：ValidateFramesContext
  - 性能对比：零拷贝 vs 传统拷贝（节省 30-50% CPU）
  - 回退策略：零拷贝失败时的软件渲染

---

### 5. 音频渲染

**跨平台、低延迟的音频输出实现。**

- 📄 [**音频渲染架构**](docs/audio_rendering.md) - 完整的音频处理流程
  - AudioPlayer 职责简化：管理播放队列、控制输出设备、跟踪播放时钟
  - AudioOutput 接口抽象：WASAPI（Windows）、ALSA（Linux）、CoreAudio（macOS）
  - AudioResampler 独立设计：解码线程预重采样，避免阻塞音频回调
  - 音频回调机制：系统驱动的实时音频输出
  - 音频时钟更新：从 FillAudioBuffer 到 AVSyncController

- 📄 [**WASAPI 深度优化**](docs/wasapi_optimization.md) - Windows 音频最佳实践
  - 共享模式 vs 独占模式的选择
  - 缓冲区大小优化：从 1 秒降至 50ms，延迟降低 10 倍
  - 轮询模式 vs 事件驱动模式
  - 音频 Underrun 检测与防止
  - 首次回调的特殊处理

- 📄 [**跨平台音频适配**](docs/cross_platform_audio.md) - 音频输出统一接口
  - AudioOutput 接口设计
  - WASAPI 实现细节（Windows）
  - ALSA 实现细节（Linux）
  - CoreAudio 实现细节（macOS）
  - 平台差异与统一抽象

---

### 6. 解码设计

**解封装与解码器的实现与优化。**

- 📄 [**解封装器设计**](docs/demuxer_design.md) - 媒体文件解析
  - Demuxer 架构：基于 FFmpeg AVFormatContext
  - 多流支持：视频流、音频流、字幕流的探测与选择
  - 数据包读取：ReadPacket 的线程安全设计
  - Seek 实现：精确跳转与关键帧搜索
  - 网络流支持：HTTP、RTSP、RTMP 协议处理

- 📄 [**解码器设计**](docs/decoder_design.md) - 音视频解码实现
  - Decoder 基类抽象：统一的解码接口
  - VideoDecoder 实现：硬件加速支持（D3D11VA、DXVA2、VA-API）
  - AudioDecoder 实现：多种音频格式支持
  - 硬件解码器上下文：HWDecoderContext 管理
  - 解码线程模型：VideoDecodeTask 与 AudioDecodeTask
  - 解码性能优化：帧池复用、零拷贝验证

- 📄 [**硬件加速详解**](docs/hardware_acceleration.md) - GPU 解码加速
  - 硬件解码类型：D3D11VA、DXVA2、VA-API、VideoToolbox
  - HWDecoderContext 初始化流程
  - AVHWDeviceContext 与 AVHWFramesContext 管理
  - 硬件帧到纹理的映射：GetD3D11Texture
  - 降级策略：硬件失败时回退到软件解码
  - 性能对比：硬件解码 vs 软件解码（节省 60-80% CPU）

---

### 7. 配置系统

**灵活的配置管理与热重载支持。**

- 📄 [**全局配置系统**](docs/global_config.md) - 配置管理器设计
  - GlobalConfig 单例模式：线程安全的全局配置
  - ConfigValue 类型系统：支持 bool、int、double、string、array、object
  - 配置文件格式：JSON 配置文件（zenplay.json）
  - 热重载机制：配置变化监听与自动重载
  - 配置监听器：ConfigChangeCallback 回调机制
  - 默认值与验证：配置缺失时的默认行为

- 📄 [**配置管理器**](docs/config_manager.md) - Loki 派遣集成
  - ConfigManager 与 Loki 任务派遣的集成
  - 异步配置加载与保存
  - 配置自动保存机制
  - 配置文件热重载监控

---

### 8. 日志系统

**强大的日志系统，支持调试与性能分析。**

- 📄 [**日志系统架构**](docs/logging_system.md) - LogManager 设计
  - spdlog 集成与配置
  - 模块化日志管理：LOG_MODULE_PLAYER、LOG_MODULE_AUDIO 等
  - 日志级别控制：TRACE、DEBUG、INFO、WARN、ERROR、CRITICAL
  - 编译期日志级别过滤：ZENPLAY_LOG_LEVEL 宏
  - 运行期日志级别动态调整
  - 日志宏设计：MODULE_DEBUG、MODULE_INFO、MODULE_ERROR 等

- 📄 [**性能测量工具**](docs/timer_util.md) - 高精度计时器
  - TIMER_START / TIMER_END 宏的使用
  - 解码耗时统计：视频解码、音频解码
  - 渲染耗时统计：纹理上传、渲染提交
  - 解封装耗时统计：数据包读取
  - 性能热点分析：识别性能瓶颈

- 📄 [**日志最佳实践**](docs/logging_best_practices.md) - 日志使用指南
  - 日志级别选择原则
  - 日志格式规范
  - 日志输出性能优化：异步日志、批量写入
  - 日志文件轮转与清理

---

### 9. 统计系统

**实时性能监控，帮助诊断和优化性能问题。**

- 📄 [**统计系统设计**](docs/statistics_system.md) - StatisticsManager 架构
  - 统计指标定义：DemuxStats、DecodeStats、RenderStats
  - 统计数据类型：帧率、缓冲区状态、延迟、丢帧数等
  - 线程安全的统计更新：原子操作与锁保护
  - 统计数据导出：JSON 格式输出
  - 性能瓶颈诊断：PerformanceBottleneck 枚举

- 📄 [**统计系统集成**](docs/stats_integration.md) - 统计宏的使用
  - STATS_UPDATE_DEMUX：解封装统计更新
  - STATS_UPDATE_DECODE：解码统计更新（视频/音频）
  - STATS_UPDATE_RENDER：渲染统计更新
  - 实时帧率计算：FPS 统计
  - 缓冲区状态监控：队列长度、缓冲水位
  - 同步误差统计：音视频同步偏差

- 📄 [**性能分析实践**](docs/performance_analysis.md) - 使用统计数据优化性能
  - 如何读取统计数据
  - 性能瓶颈识别：解封装、解码、渲染
  - 优化案例分析：WASAPI 缓冲区优化、零拷贝优化等
  - 性能回归测试

---

### 10. 线程模型

**多线程协作是高性能播放器的基础。**

- 📄 [**线程模型详解**](docs/threading_model.md) - 完整的线程架构
  - **5 个核心线程**：
    1. DemuxTask（解封装线程）：读取媒体文件，产生 AVPacket
    2. VideoDecodeTask（视频解码线程）：解码视频帧
    3. AudioDecodeTask（音频解码线程）：解码音频帧 + 预重采样
    4. VideoRenderThread（视频渲染线程）：独立渲染线程
    5. AudioCallback（音频回调线程）：系统驱动的音频输出
  - **线程间通信**：BlockingQueue（生产者-消费者模型）
  - **线程同步机制**：std::atomic、std::mutex、std::condition_variable
  - **线程生命周期管理**：启动、暂停、恢复、停止
  - **死锁预防策略**：锁顺序、超时机制、避免嵌套锁

- 📄 [**Seek 专用线程**](docs/seek_thread.md) - 异步 Seek 实现
  - SeekTask 设计：专用 Seek 线程
  - Seek 请求队列：BlockingQueue<SeekRequest>
  - Seek 流程：暂停 → 清空队列 → 跳转 → 恢复
  - 状态恢复：Seek 后恢复原始播放状态（Playing/Paused）
  - Seek 优化：音频缓冲区 Flush、快速重同步

---

### 附录：辅助文档

- 📄 [**错误处理系统**](docs/error_handling.md) - Result<T> 与错误传播
- 📄 [**内存管理策略**](docs/memory_management.md) - 智能指针与对象池
- 📄 [**代码规范**](docs/coding_style.md) - C++17 最佳实践
- 📄 [**调试技巧**](docs/debugging_tips.md) - 常见问题排查指南

---

## 🤝 贡献指南

欢迎任何形式的贡献！无论是：

- 🐛 报告 Bug
- 💡 提出新功能建议
- 📝 改进文档
- 🔧 提交代码修复或新功能

### 贡献流程

1. **Fork** 本仓库
2. 创建新分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 **Pull Request**

### 代码规范

- 遵循 C++17 标准
- 使用 `.clang-format` 格式化代码
- 为新功能添加单元测试
- 更新相关文档

### 提交信息规范

```
<type>(<scope>): <subject>

<body>

<footer>
```

**类型**：
- `feat`: 新功能
- `fix`: Bug 修复
- `docs`: 文档更新
- `style`: 代码格式（不影响功能）
- `refactor`: 重构
- `perf`: 性能优化
- `test`: 测试相关
- `chore`: 构建/工具相关

---

## 📄 许可证

本项目采用 **MIT License** 开源协议。详见 [LICENSE](LICENSE) 文件。

```
MIT License

Copyright (c) 2024 ZenPlay Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 🙏 致谢

### 开源项目

感谢以下优秀的开源项目：

- [FFmpeg](https://ffmpeg.org/) - 强大的多媒体处理框架
- [SDL2](https://www.libsdl.org/) - 跨平台多媒体库
- [Qt](https://www.qt.io/) - 跨平台 GUI 框架
- [spdlog](https://github.com/gabime/spdlog) - 高性能日志库
- [nlohmann/json](https://github.com/nlohmann/json) - 现代 C++ JSON 库
- [Conan](https://conan.io/) - C/C++ 包管理器

### 学习资源

- [雷霄骅的博客](https://blog.csdn.net/leixiaohua1020) - 音视频技术入门
- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [SDL Wiki](https://wiki.libsdl.org/)

---

## 📧 联系方式

- **项目主页**：[GitHub](https://github.com/Sunshine334419520/zenplay)
- **问题反馈**：[Issues](https://github.com/Sunshine334419520/zenplay/issues)
- **邮箱**：sunshine334419520@gmail.com

---

<div align="center">

**⭐ 如果这个项目对你有帮助，请给个 Star！⭐**

Made with ❤️ by ZenPlay Contributors

</div>
