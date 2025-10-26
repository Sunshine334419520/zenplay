#include "player/codec/hw_decoder_context.h"

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

#ifdef _WIN32
  // 1. 创建硬件设备上下文
  AVHWDeviceType hw_type;
  if (decoder_type == HWDecoderType::kD3D11VA) {
    hw_type = AV_HWDEVICE_TYPE_D3D11VA;
    MODULE_INFO(LOG_MODULE_DECODER, "Initializing D3D11VA hardware decoder");
  } else if (decoder_type == HWDecoderType::kDXVA2) {
    hw_type = AV_HWDEVICE_TYPE_DXVA2;
    MODULE_INFO(LOG_MODULE_DECODER, "Initializing DXVA2 hardware decoder");
  } else {
    return Result<void>::Err(ErrorCode::kNotSupported,
                             "Unsupported hardware decoder type");
  }

  // 创建设备上下文
  int ret =
      av_hwdevice_ctx_create(&hw_device_ctx_, hw_type, nullptr, nullptr, 0);
  if (ret < 0) {
    return FFmpegErrorToResult(ret, "Failed to create HW device context");
  }

  // 2. 提取 D3D11 设备指针
  if (decoder_type == HWDecoderType::kD3D11VA) {
    AVHWDeviceContext* device_ctx = (AVHWDeviceContext*)hw_device_ctx_->data;
    AVD3D11VADeviceContext* d3d11_ctx =
        (AVD3D11VADeviceContext*)device_ctx->hwctx;
    d3d11_device_ = d3d11_ctx->device;
    d3d11_device_context_ = d3d11_ctx->device_context;

    MODULE_INFO(LOG_MODULE_DECODER, "D3D11 device: {}, context: {}",
                (void*)d3d11_device_, (void*)d3d11_device_context_);
  }

  // 3. 创建硬件帧上下文
  auto frame_result = CreateHWFramesContext(width, height);
  if (!frame_result.IsOk()) {
    Cleanup();
    return frame_result;
  }

  MODULE_INFO(LOG_MODULE_DECODER, "HW decoder context initialized: {}x{}",
              width, height);
  return Result<void>::Ok();
#else
  return Result<void>::Err(ErrorCode::kNotSupported,
                           "Hardware decoding not supported on this platform");
#endif
}

Result<void> HWDecoderContext::CreateHWFramesContext(int width, int height) {
  // 分配帧上下文
  hw_frames_ctx_ = av_hwframe_ctx_alloc(hw_device_ctx_);
  if (!hw_frames_ctx_) {
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate HW frames context");
  }

  AVHWFramesContext* frames_ctx = (AVHWFramesContext*)hw_frames_ctx_->data;

#ifdef _WIN32
  // 配置帧参数
  if (decoder_type_ == HWDecoderType::kD3D11VA) {
    frames_ctx->format = AV_PIX_FMT_D3D11;    // 硬件格式
    frames_ctx->sw_format = AV_PIX_FMT_NV12;  // 软件回退格式
    hw_pix_fmt_ = AV_PIX_FMT_D3D11;
  } else if (decoder_type_ == HWDecoderType::kDXVA2) {
    frames_ctx->format = AV_PIX_FMT_DXVA2_VLD;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    hw_pix_fmt_ = AV_PIX_FMT_DXVA2_VLD;
  }
#endif

  frames_ctx->width = width;
  frames_ctx->height = height;
  frames_ctx->initial_pool_size = 20;  // 初始帧池大小

  // 初始化帧上下文
  int ret = av_hwframe_ctx_init(hw_frames_ctx_);
  if (ret < 0) {
    av_buffer_unref(&hw_frames_ctx_);
    return FFmpegErrorToResult(ret, "Failed to initialize HW frames context");
  }

  return Result<void>::Ok();
}

Result<void> HWDecoderContext::ConfigureDecoder(AVCodecContext* codec_ctx) {
  if (!IsInitialized()) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "HW decoder context not initialized");
  }

  // 设置硬件设备和帧上下文
  codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
  codec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx_);

  // 设置格式选择回调
  codec_ctx->get_format = GetHWFormat;
  codec_ctx->opaque = this;  // 传递上下文指针

  // 启用硬件加速相关选项
  codec_ctx->extra_hw_frames = 8;  // 额外的硬件帧缓冲

  MODULE_INFO(LOG_MODULE_DECODER,
              "Decoder configured for hardware acceleration");
  return Result<void>::Ok();
}

AVPixelFormat HWDecoderContext::GetHWFormat(AVCodecContext* ctx,
                                            const AVPixelFormat* pix_fmts) {
  HWDecoderContext* hw_ctx = static_cast<HWDecoderContext*>(ctx->opaque);

  // 查找支持的硬件格式
  for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
    if (*p == hw_ctx->hw_pix_fmt_) {
      MODULE_DEBUG(LOG_MODULE_DECODER, "Selected HW pixel format: {}",
                   av_get_pix_fmt_name(*p));
      return *p;
    }
  }

  MODULE_ERROR(LOG_MODULE_DECODER, "Failed to find HW pixel format");
  return AV_PIX_FMT_NONE;
}

#ifdef _WIN32
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

void HWDecoderContext::Cleanup() {
  if (hw_frames_ctx_) {
    av_buffer_unref(&hw_frames_ctx_);
    hw_frames_ctx_ = nullptr;
  }

  if (hw_device_ctx_) {
    av_buffer_unref(&hw_device_ctx_);
    hw_device_ctx_ = nullptr;
  }

#ifdef _WIN32
  d3d11_device_ = nullptr;
  d3d11_device_context_ = nullptr;
#endif

  MODULE_INFO(LOG_MODULE_DECODER, "HW decoder context cleaned up");
}

}  // namespace zenplay
