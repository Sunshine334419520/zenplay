#include "player/codec/hw_decoder_context.h"

#include "player/codec/hw_decoder_type.h"
#include "player/common/ffmpeg_error_utils.h"
#include "player/common/log_manager.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace zenplay {

HWDecoderContext::~HWDecoderContext() {
  Cleanup();
}

Result<void> HWDecoderContext::Initialize(HWDecoderType decoder_type,
                                          AVCodecID codec_id,
                                          int width,
                                          int height) {
  decoder_type_ = decoder_type;

  // 1. 检查平台支持
  if (!HWDecoderTypeUtil::IsSupported(decoder_type)) {
    return Result<void>::Err(
        ErrorCode::kNotSupported,
        std::string("Hardware decoder not supported on this platform: ") +
            HWDecoderTypeUtil::GetName(decoder_type));
  }

  // 2. 转换为 FFmpeg 硬件设备类型
  AVHWDeviceType hw_type = HWDecoderTypeUtil::ToFFmpegType(decoder_type);
  if (hw_type == AV_HWDEVICE_TYPE_NONE) {
    return Result<void>::Err(ErrorCode::kNotSupported,
                             "Invalid hardware decoder type");
  }

  MODULE_INFO(LOG_MODULE_DECODER, "Initializing {} hardware decoder",
              HWDecoderTypeUtil::GetName(decoder_type));

  // 3. 创建硬件设备上下文
  int ret =
      av_hwdevice_ctx_create(&hw_device_ctx_, hw_type, nullptr, nullptr, 0);
  if (ret < 0) {
    return FFmpegErrorToResult(ret, "Failed to create HW device context");
  }

  // 4. 平台特定的初始化
#ifdef OS_WIN
  if (decoder_type == HWDecoderType::kD3D11VA) {
    // 提取 D3D11 设备指针
    AVHWDeviceContext* device_ctx = (AVHWDeviceContext*)hw_device_ctx_->data;
    AVD3D11VADeviceContext* d3d11_ctx =
        (AVD3D11VADeviceContext*)device_ctx->hwctx;
    d3d11_device_ = d3d11_ctx->device;
    d3d11_device_context_ = d3d11_ctx->device_context;

    MODULE_INFO(LOG_MODULE_DECODER, "D3D11 device: {}, context: {}",
                (void*)d3d11_device_, (void*)d3d11_device_context_);

    // 设置硬件像素格式
    hw_pix_fmt_ = AV_PIX_FMT_D3D11;
  } else if (decoder_type == HWDecoderType::kDXVA2) {
    hw_pix_fmt_ = AV_PIX_FMT_DXVA2_VLD;
  }
#endif

  MODULE_INFO(LOG_MODULE_DECODER,
              "HW decoder context initialized: {}x{}, type: {}", width, height,
              HWDecoderTypeUtil::GetName(decoder_type));
  return Result<void>::Ok();
}

Result<void> HWDecoderContext::ConfigureDecoder(AVCodecContext* codec_ctx) {
  if (!IsInitialized()) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "HW decoder context not initialized");
  }

  // 设置硬件设备上下文
  codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx_);

  // 设置 get_format 回调（FFmpeg 会在解码器初始化时调用）
  codec_ctx->get_format = GetHWFormat;
  codec_ctx->opaque = this;

  // ✅ 关键：不要在这里创建 hw_frames_ctx，让 FFmpeg 在 get_format 回调中通过
  //    avcodec_get_hw_frames_parameters 创建

  MODULE_INFO(LOG_MODULE_DECODER,
              "Decoder configured for hardware acceleration (frames_ctx will "
              "be created by FFmpeg)");
  return Result<void>::Ok();
}

AVPixelFormat HWDecoderContext::GetHWFormat(AVCodecContext* ctx,
                                            const AVPixelFormat* pix_fmts) {
  HWDecoderContext* hw_ctx = static_cast<HWDecoderContext*>(ctx->opaque);
  if (!hw_ctx) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Invalid opaque pointer in GetHWFormat");
    return AV_PIX_FMT_NONE;
  }

  // 查找支持的硬件格式
  for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
    if (*p == hw_ctx->hw_pix_fmt_) {
      MODULE_DEBUG(LOG_MODULE_DECODER, "Selected HW pixel format: {}",
                   av_get_pix_fmt_name(*p));

      // ✅ 关键：像 MPV 一样，在 get_format 中创建 hw_frames_ctx
      // 但使用 FFmpeg API 而不是手动创建
      if (!hw_ctx->frames_ctx_created_ && ctx->hw_frames_ctx == nullptr) {
        MODULE_INFO(LOG_MODULE_DECODER,
                    "Creating hw_frames_ctx via FFmpeg API (like MPV)");

        auto result = hw_ctx->InitGenericHWAccel(ctx, *p);
        if (result.IsOk()) {
          hw_ctx->frames_ctx_created_ = true;
        } else {
          MODULE_ERROR(LOG_MODULE_DECODER,
                       "Failed to init hw_frames_ctx: {}, falling back to SW",
                       result.Message());
          return AV_PIX_FMT_NONE;  // 强制软件解码
        }
      }

      return *p;
    }
  }

  MODULE_ERROR(LOG_MODULE_DECODER, "Failed to find HW pixel format");
  return AV_PIX_FMT_NONE;
}

Result<void> HWDecoderContext::InitGenericHWAccel(AVCodecContext* ctx,
                                                  AVPixelFormat hw_fmt) {
  MODULE_INFO(LOG_MODULE_DECODER,
              "Initializing generic hwaccel (MPV-style) for format: {}",
              av_get_pix_fmt_name(hw_fmt));

  // ✅ 关键：使用 FFmpeg API 创建 hw_frames_ctx，而不是手动创建
  AVBufferRef* new_frames_ctx = nullptr;
  int ret = avcodec_get_hw_frames_parameters(ctx, hw_device_ctx_, hw_fmt,
                                             &new_frames_ctx);
  if (ret < 0) {
    return FFmpegErrorToResult(
        ret,
        "avcodec_get_hw_frames_parameters failed - codec may not "
        "support hardware decoding");
  }

  AVHWFramesContext* frames_ctx =
      reinterpret_cast<AVHWFramesContext*>(new_frames_ctx->data);

  MODULE_INFO(LOG_MODULE_DECODER,
              "FFmpeg calculated frames context: format={}, sw_format={}, "
              "{}x{}, initial_pool_size={}",
              av_get_pix_fmt_name(frames_ctx->format),
              av_get_pix_fmt_name(frames_ctx->sw_format), frames_ctx->width,
              frames_ctx->height, frames_ctx->initial_pool_size);

  // ✅ 调整池大小（参考 MPV hwdec_extra_frames）
  // FFmpeg 已经计算了基础池大小，我们只需要加上额外的缓冲
  if (frames_ctx->initial_pool_size > 0) {
    int extra_frames = 6;  // 参考 MPV 的 hwdec_extra_frames 默认值
    frames_ctx->initial_pool_size += extra_frames;

    MODULE_INFO(LOG_MODULE_DECODER,
                "Adjusted pool_size: {} (FFmpeg base + {} extra)",
                frames_ctx->initial_pool_size, extra_frames);
  } else {
    MODULE_INFO(LOG_MODULE_DECODER,
                "Pool size = 0 (dynamic allocation enabled by FFmpeg)");
  }

#ifdef OS_WIN
  // ✅ D3D11 特定：设置 BindFlags 以支持零拷贝
  if (decoder_type_ == HWDecoderType::kD3D11VA) {
    AVD3D11VAFramesContext* d3d11_frames_ctx =
        reinterpret_cast<AVD3D11VAFramesContext*>(frames_ctx->hwctx);

    // 添加 SHADER_RESOURCE flag（保留 FFmpeg 设置的其他 flags）
    d3d11_frames_ctx->BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    MODULE_INFO(LOG_MODULE_DECODER,
                "D3D11: Added SHADER_RESOURCE flag, BindFlags = 0x{:X}",
                d3d11_frames_ctx->BindFlags);
  }
#endif

  // ✅ 初始化帧上下文
  ret = av_hwframe_ctx_init(new_frames_ctx);
  if (ret < 0) {
    av_buffer_unref(&new_frames_ctx);
    return FFmpegErrorToResult(ret, "av_hwframe_ctx_init failed");
  }

  // ✅ 赋值给解码器（FFmpeg 接管所有权）
  ctx->hw_frames_ctx = new_frames_ctx;

  MODULE_INFO(LOG_MODULE_DECODER,
              "✅ hw_frames_ctx initialized successfully via FFmpeg API");

  return Result<void>::Ok();
}

#ifdef OS_WIN
ID3D11Texture2D* HWDecoderContext::GetD3D11Texture(AVFrame* frame) {
  if (!frame || frame->format != AV_PIX_FMT_D3D11) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Invalid frame format for D3D11 texture");
    return nullptr;
  }

  // AVFrame::data[0] 存储的是 ID3D11Texture2D*
  // AVFrame::data[1] 存储的是纹理数组索引
  return reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
}

ID3D11Device* HWDecoderContext::GetD3D11Device() const {
  return d3d11_device_;
}

ID3D11DeviceContext* HWDecoderContext::GetD3D11DeviceContext() const {
  return d3d11_device_context_;
}
#endif

bool HWDecoderContext::ValidateFramesContext(AVCodecContext* codec_ctx) const {
  if (!codec_ctx || !codec_ctx->hw_frames_ctx) {
    MODULE_WARN(LOG_MODULE_DECODER, "No hw_frames_ctx to validate");
    return false;
  }

  AVHWFramesContext* frames_ctx =
      reinterpret_cast<AVHWFramesContext*>(codec_ctx->hw_frames_ctx->data);

  MODULE_INFO(LOG_MODULE_DECODER,
              "Validating frames context: format={}, sw_format={}, {}x{}",
              av_get_pix_fmt_name(frames_ctx->format),
              av_get_pix_fmt_name(frames_ctx->sw_format), frames_ctx->width,
              frames_ctx->height);

#ifdef OS_WIN
  if (decoder_type_ == HWDecoderType::kD3D11VA) {
    AVD3D11VAFramesContext* d3d11_ctx =
        reinterpret_cast<AVD3D11VAFramesContext*>(frames_ctx->hwctx);

    MODULE_INFO(LOG_MODULE_DECODER, "D3D11 frames context BindFlags: 0x{:X}",
                d3d11_ctx->BindFlags);

    // 检查是否包含零拷贝所需的标志
    bool has_decoder_flag = (d3d11_ctx->BindFlags & D3D11_BIND_DECODER) != 0;
    bool has_srv_flag =
        (d3d11_ctx->BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;

    if (has_decoder_flag && has_srv_flag) {
      MODULE_INFO(LOG_MODULE_DECODER,
                  "✅ Zero-copy enabled: BindFlags contains both DECODER and "
                  "SHADER_RESOURCE");
      return true;
    } else {
      MODULE_WARN(LOG_MODULE_DECODER,
                  "⚠️ Zero-copy may not work: BindFlags missing required flags "
                  "(DECODER={}, SHADER_RESOURCE={})",
                  has_decoder_flag, has_srv_flag);
      return false;
    }
  }
#endif

  return true;
}

void HWDecoderContext::Cleanup() {
  if (hw_device_ctx_) {
    av_buffer_unref(&hw_device_ctx_);
    hw_device_ctx_ = nullptr;
  }

  // 重置状态标志
  frames_ctx_created_ = false;

#ifdef OS_WIN
  d3d11_device_ = nullptr;
  d3d11_device_context_ = nullptr;
#endif

  MODULE_INFO(LOG_MODULE_DECODER, "HW decoder context cleaned up");
}

}  // namespace zenplay
