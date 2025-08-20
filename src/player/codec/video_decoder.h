#pragma once

#include "player/codec/decode.h"

namespace zenplay {

class VideoDecoder : public Decoder {
 public:
  bool Open(AVCodecParameters* codec_params, AVDictionary** options = nullptr) {
    if (!codec_params || codec_params->codec_type != AVMEDIA_TYPE_VIDEO) {
      return false;  // Ensure codec parameters are valid and for video
    }
    return Decoder::Open(codec_params, options);
  }

  int width() const {
    if (!codec_context_) {
      return 0;  // Not opened
    }
    return codec_context_->width;
  }

  int height() const {
    if (!codec_context_) {
      return 0;  // Not opened
    }
    return codec_context_->height;
  }

  AVRational time_base() const {
    if (!codec_context_) {
      return {0, 1};  // Not opened
    }
    return codec_context_->time_base;
  }

  AVPixelFormat pixel_format() const {
    if (!codec_context_) {
      return AV_PIX_FMT_NONE;  // Not opened
    }
    return static_cast<AVPixelFormat>(codec_context_->pix_fmt);
  }
};

}  // namespace zenplay
