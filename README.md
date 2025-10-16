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

## ✨ 功能特性

### 已实现功能 ✅

#### 播放控制
- ✅ 播放/暂停/停止
- ✅ 快进/快退 Seek
- ✅ 音量调节
- ✅ 全屏切换

#### 媒体支持
- ✅ 本地文件播放
- ✅ 网络流媒体播放（HTTP/RTSP/RTMP）
- ✅ 多种视频编码（H.264/H.265/VP9 等）
- ✅ 多种音频编码（AAC/MP3/FLAC/Opus 等）

#### 技术特性
- ✅ 精确的音视频同步（误差 < 50ms）
- ✅ 硬件加速视频渲染
- ✅ 低延迟音频输出（WASAPI 50ms 缓冲）
- ✅ 实时性能统计和监控
- ✅ 完善的日志系统
- ✅ 线程安全的队列管理

#### UI 功能
- ✅ 现代化深色主题界面
- ✅ 实时播放进度显示
- ✅ 拖动进度条快速定位
- ✅ 响应式窗口布局
- ✅ 菜单栏和工具栏

### 规划中功能 🚧

- 🚧 播放列表管理
- 🚧 字幕支持
- 🚧 视频滤镜效果
- 🚧 截图功能
- 🚧 录制功能
- 🚧 音轨/字幕切换
- 🚧 倍速播放

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

## 📁 项目结构

```
zenplay/
├── CMakeLists.txt              # CMake 构建配置
├── conanfile.py                # Conan 依赖管理
├── README.md                   # 项目说明文档
│
├── src/                        # 源代码目录
│   ├── main.cpp                # 程序入口
│   ├── player/                 # 播放器核心
│   │   ├── zen_player.cpp/h    # 播放器主类（应用层接口）
│   │   ├── playback_controller.cpp/h  # 播放控制器（协调器）
│   │   ├── audio/              # 音频子系统
│   │   │   ├── audio_player.cpp/h     # 音频播放器
│   │   │   ├── audio_output.h         # 音频输出接口
│   │   │   └── impl/                  # 平台实现
│   │   │       ├── wasapi_audio_output.cpp/h  # Windows WASAPI
│   │   │       └── alsa_audio_output.cpp/h    # Linux ALSA
│   │   ├── video/              # 视频子系统
│   │   │   ├── video_player.cpp/h     # 视频播放器
│   │   │   └── render/                # 渲染模块
│   │   │       ├── renderer.h         # 渲染器接口
│   │   │       ├── sdl_renderer.cpp/h # SDL2 实现
│   │   │       └── renderer_proxy.cpp/h # 线程安全代理
│   │   ├── codec/              # 编解码模块
│   │   │   ├── decode.cpp/h           # 解码器基类
│   │   │   ├── audio_decoder.h        # 音频解码器
│   │   │   └── video_decoder.h        # 视频解码器
│   │   ├── demuxer/            # 解封装模块
│   │   │   └── demuxer.cpp/h          # FFmpeg 解封装
│   │   ├── sync/               # 同步控制
│   │   │   └── av_sync_controller.cpp/h  # 音视频同步
│   │   ├── common/             # 公共组件
│   │   │   ├── log_manager.cpp/h      # 日志管理
│   │   │   ├── timer.cpp/h            # 计时器工具
│   │   │   ├── player_state_manager.cpp/h  # 状态管理
│   │   │   └── common_def.h           # 公共定义
│   │   └── stats/              # 统计系统
│   │       ├── statistics_manager.cpp/h  # 统计管理器
│   │       └── stats_types.h             # 统计类型定义
│   └── view/                   # UI 界面
│       └── main_window.cpp/h   # 主窗口
│
├── docs/                       # 技术文档（详细设计文档）
│   ├── zenplay_architecture_analysis.md    # 架构分析
│   ├── audio_video_sync_design.md          # 音视频同步设计
│   ├── threading_guide.md                  # 线程模型指南
│   ├── statistics_system_design.md         # 统计系统设计
│   ├── logging_configuration.md            # 日志系统配置
│   ├── sdl_renderer_guide.md               # SDL 渲染指南
│   ├── wasapi_buffer_size_analysis.md      # WASAPI 缓冲区分析
│   └── ...                                 # 更多技术文档
│
├── resources/                  # 资源文件
│   ├── zenplay.qrc             # Qt 资源文件
│   ├── icons/                  # 图标资源
│   └── styles/                 # 样式文件
│       └── dark_theme.qss      # 深色主题
│
├── third_party/                # 第三方库
│   └── loki/                   # Loki 线程框架
│
└── examples/                   # 示例代码
    ├── sdl_renderer_demo.cpp   # SDL 渲染示例
    └── renderer_proxy_async_usage.cpp  # 异步渲染示例
```

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

### 常见问题

#### Q: Conan 安装依赖失败？

**A**: 检查 Conan 版本（需要 2.x）和网络连接。如果 Qt6 构建超时，可以增加超时时间：

```bash
conan config set general.build_timeout=3600
```

#### Q: CMake 找不到依赖？

**A**: 确保运行了 `conan install` 并且使用了正确的 preset：

```bash
cmake --preset conan-default
```

#### Q: 编译时报 C++ 标准错误？

**A**: 确保编译器支持 C++17：

```bash
# Linux/macOS
g++ --version  # 需要 GCC 9+
clang++ --version  # 需要 Clang 10+

# Windows (Visual Studio)
# 需要 Visual Studio 2019 或更高版本
```

#### Q: Linux 下缺少音频/视频驱动？

**A**: 安装必要的开发库：

```bash
# Ubuntu/Debian
sudo apt-get install libasound2-dev libsdl2-dev

# Fedora/RHEL
sudo dnf install alsa-lib-devel SDL2-devel
```

---

## 📚 技术文档

### 核心技术

以下文档详细介绍了项目的核心技术实现和设计决策。

#### 音视频同步

音视频同步是播放器最核心的技术之一，决定了用户体验的流畅度。

- 📄 [**音视频同步设计**](docs/audio_video_sync_design.md) - 同步算法原理和实现
  - PTS/DTS 时间戳管理
  - 主时钟选择策略（音频时钟）
  - 视频帧显示时机计算
  - 丢帧和重复帧策略
  - 时钟漂移补偿算法

- 📄 [**音视频同步分析**](docs/audio_video_sync_analysis.md) - 同步性能分析
  - 同步精度测试（< 50ms）
  - 时钟更新频率优化
  - Seek 后的快速重同步
  - 网络抖动处理

#### 视频渲染

高性能、低延迟的视频渲染是流畅播放的关键。

- 📄 [**SDL 渲染器指南**](docs/sdl_renderer_guide.md) - SDL2 渲染实现
  - 硬件加速纹理创建
  - YUV 到 RGB 转换
  - 双缓冲和垂直同步
  - 渲染性能优化

- 📄 [**渲染代理设计**](docs/renderer_proxy_design.md) - 线程安全渲染
  - RendererProxy 架构设计
  - 跨线程渲染调用
  - 渲染命令队列
  - UI 线程和渲染线程协调

- 📄 [**渲染代理使用**](docs/renderer_proxy_usage.md) - 使用指南和最佳实践

- 📄 [**渲染线程分析**](docs/render_thread_analysis.md) - 渲染线程模型
  - VideoRenderThread 实现
  - 帧显示时机计算
  - 丢帧策略优化
  - 渲染性能监控

#### 音频渲染

跨平台、低延迟的音频输出是良好音质的保证。

- 📄 [**WASAPI 缓冲区分析**](docs/wasapi_buffer_size_analysis.md) - Windows 音频优化
  - WASAPI 共享模式 vs 独占模式
  - 缓冲区大小对延迟的影响（1 秒 → 50ms）
  - 轮询模式 vs 事件驱动模式
  - 音频 Underrun 检测和防止

- 📄 [**WASAPI 首次回调解析**](docs/wasapi_first_callback_explained.md) - 音频启动流程
  - WASAPI 初始化流程
  - 首次回调的特殊行为
  - 大缓冲区填充策略

- 📄 [**音频架构分析**](docs/audio_architecture_analysis.md) - 音频子系统设计
  - AudioPlayer 架构
  - 重采样流程（SwrContext）
  - 跨平台音频输出抽象
  - 音频时钟更新机制

#### 线程模型

合理的多线程设计是高性能播放器的基础。

- 📄 [**线程模型指南**](docs/threading_guide.md) - 完整的线程架构
  - DemuxTask（解封装线程）
  - VideoDecodeTask（视频解码线程）
  - AudioDecodeTask（音频解码线程）
  - VideoRenderThread（视频渲染线程）
  - AudioCallback（音频回调线程）
  - 线程间通信和同步机制
  - 死锁预防策略

#### 日志系统

强大的日志系统帮助调试和性能分析。

- 📄 [**日志系统配置**](docs/logging_configuration.md) - 日志管理
  - spdlog 集成和配置
  - 编译期和运行期日志级别
  - 模块化日志管理
  - 性能优化（异步日志）
  - 日志文件轮转

- 📄 [**计时器工具指南**](docs/timer_util_guide.md) - 性能测量
  - 高精度计时器（TIMER_START/TIMER_END）
  - 解码/渲染/解封装耗时统计
  - 性能热点分析

#### 统计系统

实时性能监控帮助发现和解决性能问题。

- 📄 [**统计系统设计**](docs/statistics_system_design.md) - 统计架构
  - 统计指标定义（解封装/解码/渲染）
  - StatisticsManager 设计
  - 线程安全的统计更新
  - 统计数据导出

- 📄 [**统计系统实现报告**](docs/statistics_system_implementation_report.md) - 实现细节
  - 统计宏的使用（STATS_UPDATE_DEMUX/DECODE/RENDER）
  - 实时帧率计算
  - 缓冲区状态监控
  - 性能瓶颈诊断

### 架构设计

了解整体架构和设计模式。

- 📄 [**ZenPlay 架构分析**](docs/zenplay_architecture_analysis.md) - **核心架构文档** ⭐
  - 分层架构设计（UI/应用/核心/组件/平台）
  - 核心组件详解（ZenPlayer、PlaybackController 等）
  - 数据流和处理流程
  - 线程模型概览
  - 架构优势和改进建议

- 📄 [**状态管理设计**](docs/state_management_comparison.md) - 播放器状态机
  - PlayerStateManager 设计
  - 状态转换规则（Idle/Playing/Paused/Seeking 等）
  - 状态同步机制
  - 观察者模式应用

- 📄 [**状态转换指南**](docs/state_transition_guide.md) - 状态转换详解
  - 合法的状态转换路径
  - 错误处理和回滚
  - 状态变更通知

#### Seek 实现

快速、准确的跳转是用户体验的重要指标。

- 📄 [**异步 Seek 实现指南**](docs/async_seek_implementation_guide.md) - Seek 架构
  - 异步 Seek 流程设计
  - Seek 请求队列管理
  - 解码器和缓冲区清理
  - Seek 后的快速重同步

- 📄 [**音频 Flush 修复总结**](docs/seek_audio_flush_fix_summary.md) - Seek 优化
  - Seek 后音频延迟问题分析
  - 缓冲区清理策略
  - 状态转换顺序优化（解决 1-2 秒延迟）

### 组件详解

深入了解各个核心组件的实现。

- 📄 [**UI 使用指南**](docs/ui_guide.md) - 用户界面
  - Qt6 界面设计
  - 深色主题实现
  - 控制栏布局
  - 事件处理机制

---

## 🗺️ 开发路线

### 已完成 ✅

- [x] 基础播放框架
- [x] FFmpeg 解封装和解码
- [x] SDL2 视频渲染
- [x] WASAPI 音频输出（Windows）
- [x] 音视频同步算法
- [x] Qt6 用户界面
- [x] 多线程架构
- [x] 日志系统
- [x] 统计系统
- [x] Seek 功能
- [x] 状态管理系统
- [x] 性能优化（缓冲区大小、同步精度）

### 进行中 🚧

- [ ] ALSA 音频输出（Linux）
- [ ] CoreAudio 音频输出（macOS）
- [ ] 播放列表管理
- [ ] 配置文件持久化

### 计划中 📋

- [ ] 字幕支持（SRT/ASS/SSA）
- [ ] 视频滤镜（亮度/对比度/饱和度）
- [ ] 截图功能
- [ ] 录制功能
- [ ] 音轨/字幕切换
- [ ] 倍速播放（0.5x - 2.0x）
- [ ] 网络流优化（缓冲策略）
- [ ] GPU 解码加速（CUDA/VA-API）
- [ ] 插件系统

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
