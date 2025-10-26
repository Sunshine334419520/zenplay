#include "player/codec/video_decoder.h"

#include "player/common/log_manager.h"

namespace zenplay {

Result<void> VideoDecoder::Open(AVCodecParameters* codec_params,
                                AVDictionary** options,
                                HWDecoderContext* hw_context) {
  if (!codec_params) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "codec_params is null");
  }
  if (codec_params->codec_type != AVMEDIA_TYPE_VIDEO) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "codec_params is not for video");
  }

  hw_context_ = hw_context;

  // 调用基类的 Open（已有的实现）
  auto result = Decoder::Open(codec_params, options);
  if (!result.IsOk()) {
    return result;
  }

  // 如果提供了硬件上下文，配置硬件加速
  if (hw_context_ && hw_context_->IsInitialized()) {
    auto hw_result = hw_context_->ConfigureDecoder(codec_context_.get());
    if (!hw_result.IsOk()) {
      MODULE_WARN(LOG_MODULE_DECODER,
                  "Failed to configure HW acceleration, fallback to SW: {}",
                  hw_result.Message());
      hw_context_ = nullptr;  // 回退到软件解码
    } else {
      MODULE_INFO(LOG_MODULE_DECODER, "Hardware decoding enabled");
    }
  } else {
    MODULE_INFO(LOG_MODULE_DECODER, "Software decoding enabled");
  }

  return Result<void>::Ok();
}

}  // namespace zenplay
