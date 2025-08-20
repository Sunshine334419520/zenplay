#pragma once

#include "player/codec/decode.h"

namespace zenplay {

class AudioDecoder : public Decoder {
 public:
  bool Open(AVCodecParameters* codec_params, AVDictionary** options = nullptr) {
    if (!codec_params || codec_params->codec_type != AVMEDIA_TYPE_AUDIO) {
      return false;  // Ensure codec parameters are valid and for audio
    }
    return Decoder::Open(codec_params, options);
  }

  AVSampleFormat sample_format() const {
    if (!codec_context_) {
      return AV_SAMPLE_FMT_NONE;  // Not opened
    }
    return static_cast<AVSampleFormat>(codec_context_->sample_fmt);
  }

  int smaple_rate() const {
    if (!codec_context_) {
      return 0;  // Not opened
    }
    return codec_context_->sample_rate;
  }

  int channels() const {
    if (!codec_context_) {
      return 0;  // Not opened
    }
    return codec_context_->channels;
  }
};

}  // namespace zenplay
