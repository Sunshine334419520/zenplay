#include "sdl_renderer.h"

#include <cstring>
#include <iostream>

extern "C" {
#include <libavutil/imgutils.h>
}

#ifdef _WIN32
#include <windows.h>

#include <SDL2/SDL_syswm.h>
#endif

namespace zenplay {

SDLRenderer::SDLRenderer()
    : window_(nullptr),
      renderer_(nullptr),
      texture_(nullptr),
      window_width_(0),
      window_height_(0),
      frame_width_(0),
      frame_height_(0),
      src_pixel_format_(AV_PIX_FMT_NONE),
      sws_context_(nullptr),
      converted_frame_(nullptr),
      converted_buffer_(nullptr),
      converted_buffer_size_(0),
      sdl_pixel_format_(SDL_PIXELFORMAT_YV12),
      sdl_initialized_(false),
      renderer_initialized_(false) {}

SDLRenderer::~SDLRenderer() {
  Cleanup();
}

bool SDLRenderer::Init(void* window_handle, int width, int height) {
  window_width_ = width;
  window_height_ = height;

  if (!InitSDL()) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return false;
  }

  if (!CreateRenderer(window_handle)) {
    std::cerr << "Failed to create SDL renderer: " << SDL_GetError()
              << std::endl;
    return false;
  }

  renderer_initialized_ = true;
  return true;
}

bool SDLRenderer::RenderFrame(AVFrame* frame) {
  if (!renderer_initialized_ || !frame) {
    return false;
  }

  // Check if we need to create/recreate texture
  if (!texture_ || frame_width_ != frame->width ||
      frame_height_ != frame->height ||
      src_pixel_format_ != static_cast<AVPixelFormat>(frame->format)) {
    if (!CreateTexture(frame->width, frame->height, frame->format)) {
      std::cerr << "Failed to create texture for frame" << std::endl;
      return false;
    }
  }

  // Update texture with frame data
  if (!UpdateTexture(frame)) {
    std::cerr << "Failed to update texture" << std::endl;
    return false;
  }

  // Clear renderer
  Clear();

  // Calculate display rectangle maintaining aspect ratio
  SDL_Rect display_rect = CalculateDisplayRect(frame_width_, frame_height_);

  // Copy texture to renderer
  if (SDL_RenderCopy(renderer_, texture_, nullptr, &display_rect) != 0) {
    std::cerr << "Failed to copy texture to renderer: " << SDL_GetError()
              << std::endl;
    return false;
  }

  // Present the frame
  Present();

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

  // SDL renderer automatically handles window resizing
  // We just need to update our stored dimensions
}

void SDLRenderer::Cleanup() {
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  if (converted_frame_) {
    av_frame_free(&converted_frame_);
  }

  if (converted_buffer_) {
    av_free(converted_buffer_);
    converted_buffer_ = nullptr;
    converted_buffer_size_ = 0;
  }

  if (sws_context_) {
    sws_freeContext(sws_context_);
    sws_context_ = nullptr;
  }

  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }

  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  if (sdl_initialized_) {
    SDL_Quit();
    sdl_initialized_ = false;
  }

  renderer_initialized_ = false;
}

const char* SDLRenderer::GetRendererName() const {
  return "SDL2 Renderer";
}

bool SDLRenderer::InitSDL() {
  if (sdl_initialized_) {
    return true;
  }

  // Initialize SDL video subsystem
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return false;
  }

  sdl_initialized_ = true;
  return true;
}

bool SDLRenderer::CreateRenderer(void* window_handle) {
#ifdef _WIN32
  // On Windows, create SDL window from existing HWND
  HWND hwnd = static_cast<HWND>(window_handle);
  if (!hwnd) {
    std::cerr << "Invalid window handle" << std::endl;
    return false;
  }

  // Create SDL window from existing window handle
  window_ = SDL_CreateWindowFrom(hwnd);
  if (!window_) {
    std::cerr << "Failed to create SDL window from handle: " << SDL_GetError()
              << std::endl;
    return false;
  }

#else
  // For other platforms, you might need different handling
  std::cerr << "Window handle support not implemented for this platform"
            << std::endl;
  return false;
#endif

  // Create SDL renderer
  renderer_ = SDL_CreateRenderer(
      window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer_) {
    // Fallback to software renderer
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
      std::cerr << "Failed to create SDL renderer: " << SDL_GetError()
                << std::endl;
      return false;
    }
  }

  // Get renderer info
  SDL_RendererInfo info;
  if (SDL_GetRendererInfo(renderer_, &info) == 0) {
    std::cout << "Using SDL renderer: " << info.name << std::endl;
    std::cout << "Renderer flags: " << info.flags << std::endl;
  }

  return true;
}

bool SDLRenderer::CreateTexture(int width, int height, int format) {
  frame_width_ = width;
  frame_height_ = height;
  src_pixel_format_ = static_cast<AVPixelFormat>(format);

  // Destroy existing texture
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  // Determine SDL pixel format based on FFmpeg format
  switch (src_pixel_format_) {
    case AV_PIX_FMT_YUV420P:
      sdl_pixel_format_ = SDL_PIXELFORMAT_YV12;
      break;
    case AV_PIX_FMT_YUV422P:
      sdl_pixel_format_ = SDL_PIXELFORMAT_YUY2;
      break;
    case AV_PIX_FMT_RGB24:
      sdl_pixel_format_ = SDL_PIXELFORMAT_RGB24;
      break;
    case AV_PIX_FMT_BGR24:
      sdl_pixel_format_ = SDL_PIXELFORMAT_BGR24;
      break;
    case AV_PIX_FMT_RGBA:
      sdl_pixel_format_ = SDL_PIXELFORMAT_RGBA32;
      break;
    case AV_PIX_FMT_BGRA:
      sdl_pixel_format_ = SDL_PIXELFORMAT_BGRA32;
      break;
    default:
      // For unsupported formats, we'll convert to YUV420P
      sdl_pixel_format_ = SDL_PIXELFORMAT_YV12;
      break;
  }

  // Create SDL texture
  texture_ = SDL_CreateTexture(renderer_, sdl_pixel_format_,
                               SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!texture_) {
    std::cerr << "Failed to create SDL texture: " << SDL_GetError()
              << std::endl;
    return false;
  }

  // Setup format conversion if needed
  AVPixelFormat target_format = AV_PIX_FMT_YUV420P;  // Default target format

  switch (sdl_pixel_format_) {
    case SDL_PIXELFORMAT_YV12:
      target_format = AV_PIX_FMT_YUV420P;
      break;
    case SDL_PIXELFORMAT_RGB24:
      target_format = AV_PIX_FMT_RGB24;
      break;
    case SDL_PIXELFORMAT_BGR24:
      target_format = AV_PIX_FMT_BGR24;
      break;
    case SDL_PIXELFORMAT_RGBA32:
      target_format = AV_PIX_FMT_RGBA;
      break;
    case SDL_PIXELFORMAT_BGRA32:
      target_format = AV_PIX_FMT_BGRA;
      break;
  }

  // Create conversion context if formats differ
  if (src_pixel_format_ != target_format) {
    if (sws_context_) {
      sws_freeContext(sws_context_);
    }

    sws_context_ =
        sws_getContext(width, height, src_pixel_format_, width, height,
                       target_format, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_context_) {
      std::cerr << "Failed to create SWS context" << std::endl;
      return false;
    }

    // Allocate converted frame
    if (converted_frame_) {
      av_frame_free(&converted_frame_);
    }

    converted_frame_ = av_frame_alloc();
    if (!converted_frame_) {
      std::cerr << "Failed to allocate converted frame" << std::endl;
      return false;
    }

    converted_frame_->format = target_format;
    converted_frame_->width = width;
    converted_frame_->height = height;

    // Allocate buffer for converted frame
    int buffer_size = av_image_get_buffer_size(target_format, width, height, 1);
    if (converted_buffer_size_ < buffer_size) {
      if (converted_buffer_) {
        av_free(converted_buffer_);
      }

      converted_buffer_ = static_cast<uint8_t*>(av_malloc(buffer_size));
      if (!converted_buffer_) {
        std::cerr << "Failed to allocate conversion buffer" << std::endl;
        return false;
      }
      converted_buffer_size_ = buffer_size;
    }

    // Setup frame data pointers
    av_image_fill_arrays(converted_frame_->data, converted_frame_->linesize,
                         converted_buffer_, target_format, width, height, 1);
  }

  return true;
}

bool SDLRenderer::UpdateTexture(AVFrame* frame) {
  if (!texture_ || !frame) {
    return false;
  }

  AVFrame* frame_to_render = frame;

  // Convert frame format if necessary
  if (sws_context_) {
    if (!ConvertFrame(frame, converted_frame_)) {
      return false;
    }
    frame_to_render = converted_frame_;
  }

  // Update SDL texture
  if (sdl_pixel_format_ == SDL_PIXELFORMAT_YV12) {
    // YUV420P format
    if (SDL_UpdateYUVTexture(
            texture_, nullptr, frame_to_render->data[0],
            frame_to_render->linesize[0], frame_to_render->data[1],
            frame_to_render->linesize[1], frame_to_render->data[2],
            frame_to_render->linesize[2]) != 0) {
      std::cerr << "Failed to update YUV texture: " << SDL_GetError()
                << std::endl;
      return false;
    }
  } else {
    // RGB formats
    if (SDL_UpdateTexture(texture_, nullptr, frame_to_render->data[0],
                          frame_to_render->linesize[0]) != 0) {
      std::cerr << "Failed to update RGB texture: " << SDL_GetError()
                << std::endl;
      return false;
    }
  }

  return true;
}

bool SDLRenderer::ConvertFrame(AVFrame* src_frame, AVFrame* dst_frame) {
  if (!sws_context_ || !src_frame || !dst_frame) {
    return false;
  }

  int result =
      sws_scale(sws_context_, src_frame->data, src_frame->linesize, 0,
                src_frame->height, dst_frame->data, dst_frame->linesize);

  return result == dst_frame->height;
}

SDL_Rect SDLRenderer::CalculateDisplayRect(int frame_width, int frame_height) {
  if (frame_width <= 0 || frame_height <= 0 || window_width_ <= 0 ||
      window_height_ <= 0) {
    return {0, 0, window_width_, window_height_};
  }

  // Calculate aspect ratios
  double frame_aspect = static_cast<double>(frame_width) / frame_height;
  double window_aspect = static_cast<double>(window_width_) / window_height_;

  SDL_Rect rect;

  if (frame_aspect > window_aspect) {
    // Frame is wider than window - fit width
    rect.w = window_width_;
    rect.h = static_cast<int>(window_width_ / frame_aspect);
    rect.x = 0;
    rect.y = (window_height_ - rect.h) / 2;
  } else {
    // Frame is taller than window - fit height
    rect.h = window_height_;
    rect.w = static_cast<int>(window_height_ * frame_aspect);
    rect.x = (window_width_ - rect.w) / 2;
    rect.y = 0;
  }

  return rect;
}

}  // namespace zenplay
