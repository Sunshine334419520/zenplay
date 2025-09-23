#include "sdl_renderer.h"

#include <cstring>

extern "C" {
#include <libavutil/imgutils.h>
}

#include "../../../common/log_manager.h"
#include "sdl_manager.h"

#ifdef _WIN32
#include <windows.h>

#include <SDL_syswm.h>
#endif

extern "C" {
#include <libswscale/swscale.h>
}

namespace zenplay {

SDLRenderer::SDLRenderer()
    : renderer_(nullptr),
      window_(nullptr),
      texture_(nullptr),
      frame_width_(0),
      frame_height_(0),
      window_width_(0),
      window_height_(0),
      src_pixel_format_(AV_PIX_FMT_NONE),
      dst_pixel_format_(AV_PIX_FMT_YUV420P),
      sws_context_(nullptr),
      converted_frame_(nullptr),
      renderer_initialized_(false) {}

SDLRenderer::~SDLRenderer() {
  Cleanup();
}

bool SDLRenderer::Init(void* window_handle, int width, int height) {
  window_width_ = width;
  window_height_ = height;

  if (!InitSDL()) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to initialize SDL: {}",
                 SDL_GetError());
    return false;
  }

  if (!CreateRenderer(window_handle)) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to create SDL renderer: {}",
                 SDL_GetError());
    return false;
  }

  renderer_initialized_ = true;
  return true;
}

bool SDLRenderer::RenderFrame(AVFrame* frame) {
  if (!renderer_initialized_ || !frame) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Cannot render: initialized={}, frame={}",
                 renderer_initialized_, frame ? "valid" : "null");
    return false;
  }

  MODULE_INFO(LOG_MODULE_RENDERER, "Rendering frame: {}x{}, format={}",
              frame->width, frame->height, frame->format);

  // Create or update texture if frame properties changed
  if (frame_width_ != frame->width || frame_height_ != frame->height ||
      src_pixel_format_ != static_cast<AVPixelFormat>(frame->format)) {
    MODULE_INFO(LOG_MODULE_RENDERER,
                "Frame properties changed, recreating texture: {}x{} -> {}x{}",
                frame_width_, frame_height_, frame->width, frame->height);
    if (!CreateTexture(frame->width, frame->height, frame->format)) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to create texture for frame");
      return false;
    }
  }

  // Update texture with frame data
  if (!UpdateTexture(frame)) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to update texture");
    return false;
  }

  Clear();

  // Calculate display rectangle maintaining aspect ratio
  SDL_Rect display_rect = CalculateDisplayRect(frame_width_, frame_height_);
  MODULE_INFO(LOG_MODULE_RENDERER, "Display rect: {}x{} at ({},{})",
              display_rect.w, display_rect.h, display_rect.x, display_rect.y);

  // Copy texture to renderer
  if (SDL_RenderCopy(renderer_, texture_, nullptr, &display_rect) != 0) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to copy texture to renderer: {}",
                 SDL_GetError());
    return false;
  }

  // Present the frame
  Present();
  MODULE_INFO(LOG_MODULE_RENDERER, "Frame presented successfully");

  return true;
}

void SDLRenderer::Clear() {
  if (renderer_) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);  // Black background
    SDL_RenderClear(renderer_);
  }
}

void SDLRenderer::Present() {
  if (renderer_) {
    SDL_RenderPresent(renderer_);
  }
}

void SDLRenderer::OnResize(int width, int height) {
  window_width_ = width;
  window_height_ = height;
}

void SDLRenderer::Cleanup() {
  if (converted_frame_) {
    av_frame_free(&converted_frame_);
    converted_frame_ = nullptr;
  }

  if (sws_context_) {
    sws_freeContext(sws_context_);
    sws_context_ = nullptr;
  }

  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }

  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  if (renderer_initialized_) {
    SDLManager::Instance().Shutdown();
    renderer_initialized_ = false;
  }
}

const char* SDLRenderer::GetRendererName() const {
  return "SDL Renderer";
}

bool SDLRenderer::InitSDL() {
  return SDLManager::Instance().Initialize();
}

bool SDLRenderer::CreateRenderer(void* window_handle) {
  if (!window_handle) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Invalid window handle");
    return false;
  }

#ifdef _WIN32
  // Windows specific implementation
  HWND hwnd = static_cast<HWND>(window_handle);
  window_ = SDL_CreateWindowFrom(hwnd);
  if (!window_) {
    MODULE_ERROR(LOG_MODULE_RENDERER,
                 "Failed to create SDL window from handle: {}", SDL_GetError());
    return false;
  }
#else
  // For other platforms, you'll need platform-specific implementations
  MODULE_ERROR(LOG_MODULE_RENDERER,
               "Window handle support not implemented for this platform");
  return false;
#endif

  // Create renderer with hardware acceleration
  renderer_ = SDL_CreateRenderer(
      window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (!renderer_) {
    // Fall back to software rendering
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to create SDL renderer: {}",
                   SDL_GetError());
      return false;
    }
  }

  // Print renderer info
  SDL_RendererInfo info;
  SDL_GetRendererInfo(renderer_, &info);
  MODULE_INFO(LOG_MODULE_RENDERER, "Using SDL renderer: {}", info.name);
  MODULE_DEBUG(LOG_MODULE_RENDERER, "Renderer flags: {}", info.flags);

  return true;
}

bool SDLRenderer::CreateTexture(int width, int height, int pixel_format) {
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  frame_width_ = width;
  frame_height_ = height;
  src_pixel_format_ = static_cast<AVPixelFormat>(pixel_format);

  // Determine SDL pixel format
  Uint32 sdl_format;
  switch (src_pixel_format_) {
    case AV_PIX_FMT_YUV420P:
      sdl_format = SDL_PIXELFORMAT_IYUV;
      dst_pixel_format_ = AV_PIX_FMT_YUV420P;  // texture expects I420/IYUV
      break;
    case AV_PIX_FMT_NV12:
      sdl_format = SDL_PIXELFORMAT_NV12;
      dst_pixel_format_ = AV_PIX_FMT_NV12;  // texture expects NV12
      break;
    case AV_PIX_FMT_NV21:
      sdl_format = SDL_PIXELFORMAT_NV21;
      dst_pixel_format_ = AV_PIX_FMT_NV21;  // texture expects NV21
      break;
    case AV_PIX_FMT_RGB24:
      sdl_format = SDL_PIXELFORMAT_RGB24;
      dst_pixel_format_ = AV_PIX_FMT_RGB24;  // texture expects RGB24
      break;
    case AV_PIX_FMT_BGR24:
      sdl_format = SDL_PIXELFORMAT_BGR24;
      dst_pixel_format_ = AV_PIX_FMT_BGR24;  // texture expects BGR24
      break;
    default:
      // For unsupported formats, convert to YUV420P
      sdl_format = SDL_PIXELFORMAT_IYUV;
      dst_pixel_format_ = AV_PIX_FMT_YUV420P;
      break;
  }

  texture_ = SDL_CreateTexture(renderer_, sdl_format,
                               SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!texture_) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to create SDL texture: {}",
                 SDL_GetError());
    return false;
  }

  return true;
}

bool SDLRenderer::UpdateTexture(AVFrame* frame) {
  if (!texture_ || !frame) {
    return false;
  }

  // If pixel format conversion is needed
  AVPixelFormat frame_fmt = static_cast<AVPixelFormat>(frame->format);
  if (frame_fmt != dst_pixel_format_) {
    return UpdateTextureWithConversion(frame);
  }

  // Direct texture update for supported formats
  if (dst_pixel_format_ == AV_PIX_FMT_YUV420P) {
    // YUV420P format - update planes separately
    if (SDL_UpdateYUVTexture(texture_, nullptr, frame->data[0],
                             frame->linesize[0], frame->data[1],
                             frame->linesize[1], frame->data[2],
                             frame->linesize[2]) != 0) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to update YUV texture: {}",
                   SDL_GetError());
      return false;
    }
  } else if (dst_pixel_format_ == AV_PIX_FMT_NV12 ||
             dst_pixel_format_ == AV_PIX_FMT_NV21) {
    // NV12/NV21 - use NV texture update (two planes: Y and interleaved UV)
    if (SDL_UpdateNVTexture(texture_, nullptr, frame->data[0],
                            frame->linesize[0], frame->data[1],
                            frame->linesize[1]) != 0) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to update NV texture: {}",
                   SDL_GetError());
      return false;
    }
  } else {
    // RGB formats - update as single plane
    if (SDL_UpdateTexture(texture_, nullptr, frame->data[0],
                          frame->linesize[0]) != 0) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to update RGB texture: {}",
                   SDL_GetError());
      return false;
    }
  }

  return true;
}

bool SDLRenderer::UpdateTextureWithConversion(AVFrame* frame) {
  // Initialize conversion context if needed
  if (!sws_context_) {
    sws_context_ =
        sws_getContext(frame->width, frame->height, src_pixel_format_,
                       frame->width, frame->height, dst_pixel_format_,
                       SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_context_) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to create SWS context");
      return false;
    }
  }

  // Allocate converted frame if needed
  if (!converted_frame_) {
    converted_frame_ = av_frame_alloc();
    if (!converted_frame_) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to allocate converted frame");
      return false;
    }

    converted_frame_->format = dst_pixel_format_;
    converted_frame_->width = frame->width;
    converted_frame_->height = frame->height;

    int ret = av_frame_get_buffer(converted_frame_, 32);
    if (ret < 0) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to allocate conversion buffer");
      return false;
    }
  }

  // Convert frame
  sws_scale(sws_context_, (const uint8_t* const*)frame->data, frame->linesize,
            0, frame->height, converted_frame_->data,
            converted_frame_->linesize);

  // Update texture with converted frame
  return UpdateTexture(converted_frame_);
}

SDL_Rect SDLRenderer::CalculateDisplayRect(int frame_width, int frame_height) {
  SDL_Rect rect;

  if (window_width_ <= 0 || window_height_ <= 0 || frame_width <= 0 ||
      frame_height <= 0) {
    rect.x = rect.y = rect.w = rect.h = 0;
    return rect;
  }

  // Calculate aspect ratios
  float window_aspect = static_cast<float>(window_width_) / window_height_;
  float frame_aspect = static_cast<float>(frame_width) / frame_height;

  if (frame_aspect > window_aspect) {
    // Frame is wider than window - fit to width
    rect.w = window_width_;
    rect.h = static_cast<int>(window_width_ / frame_aspect);
    rect.x = 0;
    rect.y = (window_height_ - rect.h) / 2;
  } else {
    // Frame is taller than window - fit to height
    rect.w = static_cast<int>(window_height_ * frame_aspect);
    rect.h = window_height_;
    rect.x = (window_width_ - rect.w) / 2;
    rect.y = 0;
  }

  return rect;
}

}  // namespace zenplay
