
#pragma once

#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include "player/common/common_def.h"

namespace zenplay {

struct AVCodecCtxDeleter {
  void operator()(AVCodecContext* ctx) const {
    if (ctx) {
      AVCodecContext* tmp = ctx;
      avcodec_free_context(&tmp);
    }
  }
};

class Decoder {
 public:
  Decoder();
  virtual ~Decoder();

  bool Open(AVCodecParameters* codec_params, AVDictionary** options = nullptr);
  void Close();

  bool Decode(AVPacket* packet, std::vector<AVFramePtr>* frames);
  bool Flush(std::vector<AVFramePtr>* frames);

  bool opened() const { return opened_; }
  AVMediaType codec_type() const { return codec_type_; }

  void FlushBuffers();

 protected:
  std::unique_ptr<AVCodecContext, AVCodecCtxDeleter> codec_context_;
  AVFramePtr workFrame_ = nullptr;
  AVMediaType codec_type_ = AVMEDIA_TYPE_UNKNOWN;
  bool opened_ = false;
};

}  // namespace zenplay
