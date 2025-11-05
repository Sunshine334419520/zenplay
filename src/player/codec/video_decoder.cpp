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

  // 保存硬件上下文（在 OnBeforeOpen 中使用）
  hw_context_ = hw_context;

  // 调用基类 Open（会在 avcodec_open2 之前调用我们的 OnBeforeOpen）
  auto result = Decoder::Open(codec_params, options);
  if (!result.IsOk()) {
    hw_context_ = nullptr;  // 清理
    return result;
  }

  // 硬件加速配置完成
  if (hw_context_) {
    MODULE_INFO(LOG_MODULE_DECODER,
                "Video decoder opened with hardware acceleration");
    MODULE_INFO(LOG_MODULE_DECODER,
                "⏳ Zero-copy validation will occur after first frame decode "
                "(when hw_frames_ctx is created)");
  } else {
    MODULE_INFO(LOG_MODULE_DECODER,
                "Video decoder opened with software decoding");
  }

  return Result<void>::Ok();
}

Result<void> VideoDecoder::OnBeforeOpen(AVCodecContext* codec_ctx) {
  // 如果有硬件上下文，在 avcodec_open2 之前配置硬件加速
  if (hw_context_ && hw_context_->IsInitialized()) {
    auto hw_result = hw_context_->ConfigureDecoder(codec_ctx);
    if (!hw_result.IsOk()) {
      MODULE_WARN(
          LOG_MODULE_DECODER,
          "Failed to configure HW acceleration, will fallback to SW: {}",
          hw_result.Message());
      hw_context_ = nullptr;      // 回退到软件解码
      return Result<void>::Ok();  // 不阻止打开，只是不使用硬件加速
    }
    MODULE_INFO(LOG_MODULE_DECODER, "Hardware acceleration configured");
  }

  return Result<void>::Ok();
}

Result<AVFrame*> VideoDecoder::ReceiveFrame() {
  // 调用基类的 ReceiveFrame
  auto result = Decoder::ReceiveFrame();

  // 如果成功接收到帧，且使用硬件加速，且尚未验证零拷贝
  if (result.IsOk() && result.Value() != nullptr && hw_context_ &&
      !zero_copy_validated_) {
    MODULE_INFO(LOG_MODULE_DECODER,
                "First hardware frame decoded, validating zero-copy setup...");

    // 验证帧上下文配置
    if (hw_context_->ValidateFramesContext(GetCodecContext())) {
      MODULE_INFO(LOG_MODULE_DECODER,
                  "🎉 Zero-copy hardware rendering is ENABLED");
    } else {
      MODULE_WARN(LOG_MODULE_DECODER,
                  "⚠️ Zero-copy validation failed! Check BindFlags in logs.");
    }

    zero_copy_validated_ = true;  // 只验证一次
  }

  return result;
}

}  // namespace zenplay
