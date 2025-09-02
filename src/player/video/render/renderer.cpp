#include "renderer.h"

#include <memory>

#include "impl/sdl_renderer.h"
#include "renderer_proxy.h"

namespace zenplay {

Renderer* Renderer::CreateRenderer() {
  // 创建实际的SDL渲染器
  auto sdl_renderer = std::make_unique<SDLRenderer>();

  // 用代理包装，确保线程安全
  return new RendererProxy(std::move(sdl_renderer));
}

}  // namespace zenplay
