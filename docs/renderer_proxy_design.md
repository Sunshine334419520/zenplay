# RendererProxy 设计说明

## 🎯 **设计目标**

创建一个完全透明的代理类，让外部调用者无需关心线程问题，所有渲染操作自动在正确的线程中执行。

## 🏗 **架构设计**

### 类图结构
```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   外部调用者    │───→│  RendererProxy   │───→│  SDLRenderer    │
│  (任意线程)     │    │  (线程安全代理)  │    │  (实际实现)     │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

### 核心机制
```cpp
template<typename ReturnT, typename Func>
ReturnT RendererProxy::EnsureUIThread(Func&& func) {
    // 1. 检查当前线程
    if (loki::LokiThread::CurrentlyOn(loki::ID::UI)) {
        return func();  // 直接执行
    }
    
    // 2. 派发到UI线程并同步等待
    return loki::Invoke<ReturnT>(
        loki::ID::UI, FROM_HERE,
        loki::FunctionView<ReturnT()>(std::forward<Func>(func))
    );
}
```

## ✅ **设计优势**

### 1. **完全透明的接口**
```cpp
// 外部代码保持不变，无需修改
Renderer* renderer = Renderer::CreateRenderer();
renderer->Init(hwnd, 800, 600);        // 自动派发到UI线程
renderer->RenderFrame(frame);          // 自动派发到UI线程
renderer->Present();                   // 自动派发到UI线程
```

### 2. **零性能损耗(当已在UI线程时)**
```cpp
// 当PlaybackController的RenderTask在loki UI线程中执行时：
void PlaybackController::RenderTask() {
    // 这里的调用不会产生线程切换开销
    renderer_->RenderFrame(frame);  // 直接调用，CurrentlyOn()返回true
}
```

### 3. **线程安全保证**
```cpp
// 从任意线程调用都是安全的：
std::thread worker([renderer]() {
    renderer->Clear();     // 安全：自动派发到UI线程
});

QtConcurrent::run([renderer]() {
    renderer->Present();   // 安全：自动派发到UI线程  
});
```

### 4. **同步执行语义**
```cpp
// 调用者可以确保操作完成后再继续
bool success = renderer->Init(hwnd, 800, 600);
if (success) {
    // 此时Init确实已经完成，可以安全地继续
    renderer->RenderFrame(first_frame);
}
```

## 🔄 **执行流程示例**

### 场景1: 从Qt主线程调用
```cpp
// Qt主线程 -> loki UI线程 -> SDL API
renderer->Init(hwnd, 800, 600);
```
```
Qt主线程 [调用Init] 
    ↓ (loki::Invoke同步等待)
loki UI线程 [执行SDL_CreateRenderer] 
    ↓ (完成后返回结果)
Qt主线程 [收到返回值，继续执行]
```

### 场景2: 从loki UI线程调用
```cpp
// loki UI线程 -> SDL API (直接调用)
renderer->RenderFrame(frame);
```
```
loki UI线程 [调用RenderFrame]
    ↓ (CurrentlyOn(UI)为true，直接执行)
loki UI线程 [SDL_RenderCopy] 
    ↓ (立即返回)
loki UI线程 [继续执行]
```

### 场景3: 从Worker线程调用
```cpp
// Worker线程 -> loki UI线程 -> SDL API
renderer->Clear();
```
```
Worker线程 [调用Clear] 
    ↓ (loki::Invoke同步等待)
loki UI线程 [执行SDL_RenderClear] 
    ↓ (完成后通知)
Worker线程 [继续执行]
```

## 🎮 **实际使用场景**

### PlaybackController中的使用
```cpp
void PlaybackController::RenderTask() {
    // 这个方法本身在loki UI线程中执行
    AVFramePtr frame = nullptr;
    if (video_frame_queue_.Pop(frame)) {
        // 直接调用，无开销，但仍然保证了线程安全
        renderer_->RenderFrame(frame.get());  // CurrentlyOn(UI) = true
    }
}

void PlaybackController::Stop() {
    // 这个方法可能在Qt主线程中调用  
    renderer_->Clear();     // 自动派发到UI线程
    renderer_->Cleanup();   // 自动派发到UI线程
}
```

### MainWindow中的使用
```cpp
void MainWindow::resizeEvent(QResizeEvent* event) {
    // Qt主线程中的窗口resize事件
    if (renderer_) {
        // 自动派发到UI线程执行
        renderer_->OnResize(event->size().width(), event->size().height());
    }
}
```

## 🔧 **实现细节**

### 错误处理
```cpp
bool RendererProxy::Init(void* window_handle, int width, int height) {
    return EnsureUIThread<bool>([this, window_handle, width, height]() {
        try {
            return actual_renderer_->Init(window_handle, width, height);
        } catch (...) {
            // 异常也会正确地传播回调用线程
            throw;
        }
    });
}
```

### 资源管理
```cpp
RendererProxy::~RendererProxy() {
    if (actual_renderer_) {
        // 确保清理在正确的线程中执行
        EnsureUIThreadVoid([this]() {
            actual_renderer_->Cleanup();
        });
    }
}
```

### 性能考虑
- **热路径优化**: `CurrentlyOn()`检查是非常快的，基本无开销
- **避免不必要的同步**: 当已在目标线程时，完全不涉及线程切换
- **lambda捕获优化**: 使用引用捕获避免不必要的拷贝

## 🎯 **这个设计的好处**

1. **外部无感知** - 调用者不需要了解线程模型
2. **类型安全** - 编译时就能确保接口正确性  
3. **性能优化** - 已在目标线程时零开销
4. **错误传播** - 异常和错误正确传播
5. **易于测试** - 可以轻松Mock actual_renderer_
6. **可扩展性** - 可以轻松添加其他渲染器后端

这个设计完美解决了SDL线程安全问题，同时保持了API的简洁性和易用性！
