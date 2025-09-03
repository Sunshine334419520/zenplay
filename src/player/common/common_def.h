#pragma once

#include <memory>

extern "C" {
#include <libavutil/frame.h>
}

namespace zenplay {

struct AVFrameDeleter {
  void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

}  // namespace zenplay