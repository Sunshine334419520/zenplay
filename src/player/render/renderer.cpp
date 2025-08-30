#include "renderer.h"

#include "impl/sdl_renderer.h"

namespace zenplay {

Renderer* Renderer::CreateRenderer() {
  // For now, only SDL renderer is implemented
  return new zenplay::SDLRenderer();
}

}  // namespace zenplay
