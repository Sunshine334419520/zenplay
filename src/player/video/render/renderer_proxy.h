#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>

#include "loki/src/post_task_interface.h"
#include "loki/src/threading/loki_thread.h"
#include "player/video/render/renderer.h"

namespace zenplay {

/**
 * @brief Renderer代理类 - 确保所有渲染操作都在loki UI线程中执行
 *
 * 这个代理类提供与Renderer完全相同的接口，但会自动处理线程调度：
 * 1. 如果当前已在loki::ID::UI线程，直接调用底层renderer
 * 2. 如果不在UI线程，使用loki::Invoke同步派发到UI线程执行
 *
 * 使用这个代理的好处：
 * - 外部代码无需关心调用线程
 * - 保证SDL等渲染API的线程安全性
 * - 接口与Renderer完全一致，可以无缝替换
 */
class RendererProxy : public Renderer {
 public:
  /**
   * @brief 创建渲染器代理
   * @param actual_renderer 实际的渲染器实现（如SDLRenderer）
   */
  explicit RendererProxy(std::unique_ptr<Renderer> actual_renderer);
  ~RendererProxy() override;

  // Renderer接口实现 - 所有方法都会确保在loki UI线程中执行
  Result<void> Init(void* window_handle, int width, int height) override;
  bool RenderFrame(AVFrame* frame) override;
  void Clear() override;
  void Present() override;
  void OnResize(int width, int height) override;
  void Cleanup() override;
  const char* GetRendererName() const override;
  virtual void ClearCaches() override;

 private:
  /**
   * @brief 确保在UI线程中执行函数
   * @tparam ReturnT 返回类型
   * @tparam Func 函数类型
   * @param func 要执行的函数
   * @return 函数执行结果
   */
  template <typename ReturnT, typename Func>
  ReturnT EnsureUIThread(Func&& func);

  /**
   * @brief 确保在UI线程中执行void函数
   * @tparam Func 函数类型
   * @param func 要执行的函数
   */
  template <typename Func>
  void EnsureUIThreadVoid(Func&& func);

 private:
  std::unique_ptr<Renderer> actual_renderer_;  // 实际的渲染器实现
  mutable std::string renderer_name_;          // 缓存渲染器名称
};

// 模板方法实现
template <typename ReturnT, typename Func>
ReturnT RendererProxy::EnsureUIThread(Func&& func) {
  // 如果当前就在UI线程，直接执行
  if (loki::LokiThread::CurrentlyOn(loki::ID::UI)) {
    return func();
  }

  // 否则使用loki::Invoke同步派发到UI线程
  return loki::Invoke<ReturnT>(
      loki::ID::UI, FROM_HERE,
      loki::FunctionView<ReturnT()>(std::forward<Func>(func)));
}

template <typename Func>
void RendererProxy::EnsureUIThreadVoid(Func&& func) {
  // 如果当前就在UI线程，直接执行
  if (loki::LokiThread::CurrentlyOn(loki::ID::UI)) {
    func();
    return;
  }

  // 否则使用loki::Invoke同步派发到UI线程
  loki::Invoke<void>(loki::ID::UI, FROM_HERE,
                     loki::FunctionView<void()>(std::forward<Func>(func)));
}

}  // namespace zenplay
