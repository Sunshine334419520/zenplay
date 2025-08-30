# SDL渲染器实现说明

## 🎯 实现特性

### ✅ 已实现功能
1. **Windows窗口句柄支持** - 通过`SDL_CreateWindowFrom()`接受外部HWND
2. **多种像素格式支持** - 自动格式转换(YUV420P, RGB24, BGR24, RGBA, BGRA等)
3. **硬件加速渲染** - 优先使用硬件加速，失败时回退到软件渲染
4. **长宽比保持** - 自动计算显示矩形，保持视频原始长宽比
5. **实时格式转换** - 使用FFmpeg的swscale进行高效格式转换
6. **资源管理** - 完整的RAII资源管理，避免内存泄漏

### 🔧 核心组件

#### 1. 格式转换系统
```cpp
// 自动检测并转换不支持的像素格式
SwsContext* sws_context_;           // FFmpeg格式转换上下文
AVFrame* converted_frame_;          // 转换后的帧缓存
uint8_t* converted_buffer_;         // 转换缓冲区
```

#### 2. 纹理管理
```cpp
SDL_Texture* texture_;              // SDL渲染纹理
Uint32 sdl_pixel_format_;          // SDL像素格式
```

#### 3. 窗口集成
```cpp
SDL_Window* window_;                // 从外部句柄创建的SDL窗口
SDL_Renderer* renderer_;            // SDL渲染器
```

## 🚀 使用方法

### 基本使用流程
```cpp
// 1. 创建播放器和渲染器
auto player = std::make_unique<VideoPlayer>();

// 2. 打开媒体文件
player->Open("video.mp4");

// 3. 设置渲染窗口 (Windows)
HWND hwnd = GetWindowHandle(); // 获取窗口句柄
player->SetRenderWindow(hwnd, width, height);

// 4. 开始播放
player->Play();
```

### Qt集成示例
```cpp
// 在Qt widget中使用
QWidget* videoWidget = new QWidget();
HWND hwnd = reinterpret_cast<HWND>(videoWidget->winId());
player->SetRenderWindow(hwnd, videoWidget->width(), videoWidget->height());
```

## 🔍 技术细节

### 像素格式映射
| FFmpeg格式 | SDL格式 | 转换需要 |
|-----------|---------|----------|
| AV_PIX_FMT_YUV420P | SDL_PIXELFORMAT_YV12 | 否 |
| AV_PIX_FMT_RGB24 | SDL_PIXELFORMAT_RGB24 | 否 |
| AV_PIX_FMT_BGR24 | SDL_PIXELFORMAT_BGR24 | 否 |
| AV_PIX_FMT_RGBA | SDL_PIXELFORMAT_RGBA32 | 否 |
| 其他格式 | SDL_PIXELFORMAT_YV12 | 是(转为YUV420P) |

### 渲染流程
1. **检查纹理** - 根据帧尺寸和格式创建/更新纹理
2. **格式转换** - 如需要，转换为SDL支持的格式  
3. **更新纹理** - 将帧数据传输到GPU纹理
4. **计算显示** - 保持长宽比计算显示矩形
5. **渲染呈现** - SDL渲染并呈现到屏幕

### 性能优化
- **硬件加速优先** - 自动检测并使用硬件渲染器
- **格式缓存** - 重用转换上下文和缓冲区
- **纹理重用** - 只有在格式/尺寸变化时才重建纹理
- **VSync支持** - 启用垂直同步减少撕裂

## ⚡ 性能特征

### 内存使用
- **最小内存占用** - 只分配必要的转换缓冲区
- **智能缓存** - 重用纹理和转换上下文
- **即时释放** - RAII确保资源及时释放

### 渲染性能  
- **GPU渲染** - 优先使用硬件加速
- **零拷贝优化** - 支持的格式直接使用，无需转换
- **批量更新** - 高效的纹理更新机制

## 🛠 扩展计划

### 第二阶段 - 高级特性
1. **多显示器支持** - 支持多屏幕渲染
2. **全屏模式** - 专用全屏渲染模式  
3. **字幕渲染** - 集成字幕显示功能
4. **OSD叠加** - 播放控制界面叠加

### 第三阶段 - 平台扩展
1. **Linux支持** - X11/Wayland窗口系统集成
2. **macOS支持** - Cocoa窗口系统集成
3. **移动平台** - Android/iOS支持

## 🐛 已知限制

1. **平台限制** - 当前仅支持Windows平台的窗口句柄
2. **格式支持** - 部分特殊像素格式需要转换，可能影响性能
3. **音频同步** - 当前版本未实现精确的音视频同步

## 📝 注意事项

1. **线程安全** - 渲染器不是线程安全的，需要在UI线程调用
2. **窗口生命周期** - 确保窗口句柄在渲染期间保持有效
3. **资源清理** - 播放器销毁时会自动清理所有SDL资源
4. **错误处理** - 所有SDL错误都会记录到日志，便于调试
