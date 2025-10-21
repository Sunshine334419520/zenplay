#pragma once

#include <string>

#include "error.h"

extern "C" {
#include <SDL2/SDL.h>
}

namespace zenplay {

// Map SDL error to ErrorCode
inline ErrorCode MapSDLError() {
  const char* error = SDL_GetError();
  if (!error || error[0] == '\0') {
    return ErrorCode::kUnknownError;
  }

  std::string error_str(error);

  // Common SDL error patterns
  if (error_str.find("Video subsystem") != std::string::npos ||
      error_str.find("video driver") != std::string::npos) {
    return ErrorCode::kRendererError;
  } else if (error_str.find("No available video device") != std::string::npos) {
    return ErrorCode::kDeviceNotFound;
  } else if (error_str.find("Out of memory") != std::string::npos) {
    return ErrorCode::kOutOfMemory;
  } else if (error_str.find("Invalid") != std::string::npos ||
             error_str.find("invalid") != std::string::npos) {
    return ErrorCode::kInvalidParameter;
  } else if (error_str.find("not supported") != std::string::npos) {
    return ErrorCode::kNotSupported;
  }

  return ErrorCode::kRendererError;
}

// Format SDL error with context
inline std::string FormatSDLError(const std::string& context) {
  const char* error = SDL_GetError();
  if (!error || error[0] == '\0') {
    return context + ": Unknown SDL error";
  }
  return context + ": " + std::string(error);
}

// Convert SDL error to Result<void>
inline Result<void> SDLErrorToResult(const std::string& context) {
  return Result<void>::Err(MapSDLError(), FormatSDLError(context));
}

// Convert SDL error to Result<T>
template <typename T>
Result<T> SDLErrorToResult(const std::string& context) {
  return Result<T>::Err(MapSDLError(), FormatSDLError(context));
}

}  // namespace zenplay
