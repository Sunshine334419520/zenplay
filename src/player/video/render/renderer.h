#pragma once

#include <cstdint>

#include "player/common/error.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace zenplay {

class Renderer {
 public:
  Renderer() = default;
  virtual ~Renderer() = default;

  // Initialize the renderer with window handle
  virtual Result<void> Init(void* window_handle, int width, int height) = 0;

  // Render a video frame
  virtual bool RenderFrame(AVFrame* frame) = 0;

  // Clear the render target
  virtual void Clear() = 0;

  // Present the rendered frame
  virtual void Present() = 0;

  // Handle window resize
  virtual void OnResize(int width, int height) = 0;

  // Cleanup resources
  virtual void Cleanup() = 0;

  // Get renderer info
  virtual const char* GetRendererName() const = 0;

  /**
   * @brief 清空所有渲染缓存
   * 在 Seek 时调用，确保没有野指针或过期资源
   * 默认实现：空（do nothing）
   */
  virtual void ClearCaches() = 0;
};

}  // namespace zenplay
