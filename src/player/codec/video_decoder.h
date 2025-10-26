#pragma once

#include "player/codec/decode.h"
#include "player/codec/hw_decoder_context.h"

namespace zenplay {

class VideoDecoder : public Decoder {
 public:
  /**
   * @brief 打开视频解码器（支持硬件加速）
   *
   * @param codec_params 编解码器参数
   * @param options FFmpeg 选项
   * @param hw_context 硬件解码上下文（可选，nullptr 表示软件解码）
   * @return Result<void>
   */
  Result<void> Open(AVCodecParameters* codec_params,
                    AVDictionary** options = nullptr,
                    HWDecoderContext* hw_context = nullptr);

  /**
   * @brief 是否使用硬件解码
   */
  bool IsHardwareDecoding() const { return hw_context_ != nullptr; }

  /**
   * @brief 获取硬件上下文
   */
  HWDecoderContext* GetHWContext() const { return hw_context_; }

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

 private:
  HWDecoderContext* hw_context_ = nullptr;  // 不拥有所有权
};

}  // namespace zenplay
