# RendererProxy 使用指南

## 🎯 **对外部的完全透明**

`RendererProxy`的最大优势就是**外部代码无需任何修改**。它完全实现了`Renderer`接口，可以无缝替换。

## 📝 **使用示例**

### 1. **工厂创建 (自动使用代理)**

```cpp
// 在 renderer.cpp 中已经修改了工厂方法
Renderer* Renderer::CreateRenderer() {
    // 创建实际的SDL渲染器
    auto sdl_renderer = std::make_unique<SDLRenderer>();
    
    // 自动用代理包装，确保线程安全
    return new RendererProxy(std::move(sdl_renderer));
}

// 外部代码完全不变
Renderer* renderer = Renderer::CreateRenderer();  // 获得的是RendererProxy实例
```

### 2. **VideoPlayer中的使用 (无需修改)**

```cpp
// src/player/video_player.cpp
class VideoPlayer {
private:
    std::unique_ptr<Renderer> renderer_;

public:
    VideoPlayer() 
        : renderer_(std::unique_ptr<Renderer>(Renderer::CreateRenderer())) {
        // renderer_ 实际上是 RendererProxy 实例，但外部无感知
    }

    bool SetRenderWindow(void* window_handle, int width, int height) {
        if (!renderer_) return false;
        
        // 这个调用会：
        // 1. 如果在loki UI线程：直接调用SDL
        // 2. 如果在Qt主线程：自动派发到loki UI线程，同步等待
        return renderer_->Init(window_handle, width, height);
    }
};
```

### 3. **PlaybackController中的使用 (无需修改)**

```cpp
// src/player/playback_controller.cpp
void PlaybackController::RenderTask() {
    // 这个方法在loki UI线程中执行
    AVFramePtr frame = nullptr;
    if (video_frame_queue_.Pop(frame)) {
        // 由于当前就在loki UI线程，代理会直接调用，无性能开销
        renderer_->RenderFrame(frame.get());  // CurrentlyOn(UI) == true
        renderer_->Present();                  // 直接调用SDL API
    }
}

void PlaybackController::Stop() {
    // 这个方法可能在任意线程调用
    if (renderer_) {
        renderer_->Clear();     // 自动确保在UI线程执行
        renderer_->Cleanup();   // 自动确保在UI线程执行
    }
}
```

### 4. **MainWindow中的使用 (无需修改)**

```cpp
// src/view/main_window.cpp
void MainWindow::resizeEvent(QResizeEvent* event) {
    // Qt主线程中的窗口事件
    if (video_player_ && video_player_->IsOpened()) {
        // 这个调用会自动派发到loki UI线程执行
        video_player_->GetRenderer()->OnResize(
            event->size().width(), 
            event->size().height()
        );
    }
    QMainWindow::resizeEvent(event);
}

void MainWindow::on_stopButton_clicked() {
    // Qt主线程中的按钮点击
    if (video_player_) {
        video_player_->Stop();  // 内部会调用renderer_->Clear()
                               // 自动派发到UI线程
    }
}
```

## 🚀 **多线程场景示例**

### 场景1: Qt主线程调用
```cpp
// Qt主线程 - 用户点击播放按钮
void MainWindow::on_playButton_clicked() {
    if (video_player_->Open("movie.mp4")) {
        // 这个调用在Qt主线程，但会自动派发到loki UI线程
        bool success = video_player_->SetRenderWindow(
            reinterpret_cast<void*>(ui->videoWidget->winId()), 
            800, 600
        );
        
        if (success) {
            video_player_->Play();
        }
    }
}
```
**执行流程：**
```
Qt主线程 [调用SetRenderWindow]
    ↓ (RendererProxy::Init)
    ↓ (检查CurrentlyOn(UI) = false)
    ↓ (loki::Invoke同步等待)
loki UI线程 [执行SDLRenderer::Init]
    ↓ (SDL_CreateRenderer等)
    ↓ (返回结果)
Qt主线程 [收到返回值，继续执行Play()]
```

### 场景2: Loki UI线程调用 (高性能路径)
```cpp
// PlaybackController::RenderTask 在loki UI线程中执行
void PlaybackController::RenderTask() {
    if (auto frame = GetNextFrame()) {
        // CurrentlyOn(loki::ID::UI) == true
        // 代理检测到已在目标线程，直接调用，零开销！
        renderer_->RenderFrame(frame.get());
        renderer_->Present();
    }
    
    // 调度下次渲染
    ScheduleNextRender();
}
```
**执行流程：**
```
loki UI线程 [调用RenderFrame]
    ↓ (RendererProxy::RenderFrame)
    ↓ (检查CurrentlyOn(UI) = true)
    ↓ (直接调用，无同步开销)
loki UI线程 [SDLRenderer::RenderFrame]
    ↓ (SDL_RenderCopy)
loki UI线程 [立即返回继续执行]
```

### 场景3: Worker线程调用
```cpp
// 假设有个后台线程需要清理渲染器
std::thread cleanup_thread([renderer]() {
    // Worker线程中调用
    renderer->Clear();    // 自动派发到UI线程
    renderer->Cleanup();  // 自动派发到UI线程
});
```

## 🔧 **实际集成示例**

### 完整的播放器初始化流程
```cpp
// 1. 创建播放器 (VideoPlayer构造函数)
VideoPlayer player;  // 内部已创建RendererProxy

// 2. 打开媒体文件 (任意线程)
bool opened = player.Open("test.mp4");

// 3. 设置渲染窗口 (通常在Qt主线程)
HWND hwnd = GetWindowHandle();
bool renderer_ready = player.SetRenderWindow(hwnd, 1920, 1080);

// 4. 开始播放 (任意线程)
player.Play();  // 内部会启动loki UI线程的RenderTask循环

// 5. 用户交互 (Qt主线程)
player.Pause();   // 线程安全
player.Seek(60);  // 线程安全  
player.Stop();    // 线程安全
```

### 错误处理示例
```cpp
try {
    // 即使在非UI线程调用，异常也会正确传播
    bool success = renderer->Init(invalid_handle, 0, 0);
    if (!success) {
        std::cerr << "Failed to initialize renderer\n";
    }
} catch (const std::exception& e) {
    // 异常从loki UI线程正确传播到调用线程
    std::cerr << "Renderer error: " << e.what() << "\n";
}
```

## 🎯 **关键优势总结**

### ✅ **对现有代码零影响**
- 所有现有的`renderer->` 调用都无需修改
- 工厂方法自动返回代理实例
- 接口完全一致

### ✅ **性能优化**  
- 已在UI线程时零开销 (热路径优化)
- 避免不必要的线程切换
- 同步等待确保操作完成

### ✅ **线程安全保证**
- SDL API调用100%在loki UI线程执行
- 避免所有跨线程渲染问题
- 支持从任意线程安全调用

### ✅ **开发体验好**
- 调用者无需了解线程模型
- 编译期类型安全
- 运行时错误正确传播

## 📊 **性能对比**

| 调用场景 | 传统方式 | RendererProxy | 性能影响 |
|---------|----------|---------------|----------|
| loki UI线程调用 | 直接调用 | 检查+直接调用 | ~0% (微秒级检查) |
| Qt主线程调用 | ❌崩溃/错误 | 自动同步派发 | 线程切换开销 |
| Worker线程调用 | ❌崩溃/错误 | 自动同步派发 | 线程切换开销 |

**结论：热路径(UI线程调用)零开销，冷路径(跨线程调用)换来了正确性和安全性！**
