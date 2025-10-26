#include "renderer.h"

#include <memory>

#include "impl/sdl/sdl_renderer.h"
#include "renderer_proxy.h"

namespace zenplay {

Renderer* Renderer::CreateRenderer() {
  // 创建实际的SDL渲染器
  auto sdl_renderer = std::make_unique<SDLRenderer>();

  // 用代理包装，确保在loki UI线程中执行，避免与Qt事件循环死锁
  return new RendererProxy(std::move(sdl_renderer));
}

}  // namespace zenplay
