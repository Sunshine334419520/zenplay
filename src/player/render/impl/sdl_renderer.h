#pragma once

#include <memory>

#include "../renderer.h"

extern "C" {
#include <SDL2/SDL.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace zenplay {

class SDLRenderer : public Renderer {
 public:
  SDLRenderer();
  ~SDLRenderer() override;

  // Renderer interface implementation
  bool Init(void* window_handle, int width, int height) override;
  bool RenderFrame(AVFrame* frame) override;
  void Clear() override;
  void Present() override;
  void OnResize(int width, int height) override;
  void Cleanup() override;
  const char* GetRendererName() const override;

 private:
  // Initialize SDL subsystems
  bool InitSDL();

  // Create SDL renderer from window handle
  bool CreateRenderer(void* window_handle);

  // Create SDL texture for video frames
  bool CreateTexture(int width, int height, int format);

  // Update texture with frame data
  bool UpdateTexture(AVFrame* frame);

  // Convert frame format if necessary
  bool ConvertFrame(AVFrame* src_frame, AVFrame* dst_frame);

  // Calculate display rectangle with aspect ratio
  SDL_Rect CalculateDisplayRect(int frame_width, int frame_height);

 private:
  // SDL objects
  SDL_Window* window_;
  SDL_Renderer* renderer_;
  SDL_Texture* texture_;

  // Video properties
  int window_width_;
  int window_height_;
  int frame_width_;
  int frame_height_;
  AVPixelFormat src_pixel_format_;

  // Format conversion
  struct SwsContext* sws_context_;
  AVFrame* converted_frame_;
  uint8_t* converted_buffer_;
  int converted_buffer_size_;

  // SDL pixel format
  Uint32 sdl_pixel_format_;

  // Initialization state
  bool sdl_initialized_;
  bool renderer_initialized_;
};

}  // namespace zenplay
