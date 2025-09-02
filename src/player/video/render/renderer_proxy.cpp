#include "renderer_proxy.h"

#include "loki/src/threading/loki_thread.h"

namespace zenplay {

RendererProxy::RendererProxy(std::unique_ptr<Renderer> actual_renderer)
    : actual_renderer_(std::move(actual_renderer)) {}

RendererProxy::~RendererProxy() {
  // 确保在UI线程中清理资源
  if (actual_renderer_) {
    EnsureUIThreadVoid([this]() { actual_renderer_->Cleanup(); });
  }
}

bool RendererProxy::Init(void* window_handle, int width, int height) {
  return EnsureUIThread<bool>([this, window_handle, width, height]() {
    return actual_renderer_->Init(window_handle, width, height);
  });
}

bool RendererProxy::RenderFrame(AVFrame* frame) {
  return EnsureUIThread<bool>(
      [this, frame]() { return actual_renderer_->RenderFrame(frame); });
}

void RendererProxy::Clear() {
  EnsureUIThreadVoid([this]() { actual_renderer_->Clear(); });
}

void RendererProxy::Present() {
  EnsureUIThreadVoid([this]() { actual_renderer_->Present(); });
}

void RendererProxy::OnResize(int width, int height) {
  EnsureUIThreadVoid(
      [this, width, height]() { actual_renderer_->OnResize(width, height); });
}

void RendererProxy::Cleanup() {
  EnsureUIThreadVoid([this]() { actual_renderer_->Cleanup(); });
}

const char* RendererProxy::GetRendererName() const {
  // 对于const方法，需要特殊处理
  if (renderer_name_.empty()) {
    // 只在第一次调用时获取名称并缓存
    if (loki::LokiThread::CurrentlyOn(loki::ID::UI)) {
      renderer_name_ = actual_renderer_->GetRendererName();
    } else {
      // 使用const_cast是安全的，因为我们只是缓存结果
      auto* mutable_this = const_cast<RendererProxy*>(this);
      renderer_name_ = loki::Invoke<std::string>(
          loki::ID::UI, FROM_HERE,
          loki::FunctionView<std::string()>([this]() -> std::string {
            return actual_renderer_->GetRendererName();
          }));
    }
  }
  return renderer_name_.c_str();
}

}  // namespace zenplay
