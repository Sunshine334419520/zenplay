#pragma once

#ifdef OS_WIN
#include <d3d11.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#ifdef OS_WIN
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/hwcontext_dxva2.h>
#endif
}

#include "player/codec/hw_decoder_type.h"
#include "player/common/error.h"

namespace zenplay {

/**
 * @brief 硬件解码器上下文管理
 *
 * 负责：
 * 1. 创建和管理 AVHWDeviceContext (D3D11 设备)
 * 2. 创建和管理 AVHWFramesContext (帧池)
 * 3. 提供硬件帧到 D3D11 纹理的访问
 */
class HWDecoderContext {
 public:
  HWDecoderContext() = default;
  ~HWDecoderContext();

  /**
   * @brief 初始化硬件解码上下文
   *
   * @param decoder_type 硬件解码器类型
   * @param codec_id 编解码器 ID
   * @param width 视频宽度
   * @param height 视频高度
   * @return Result<void>
   */
  Result<void> Initialize(HWDecoderType decoder_type,
                          AVCodecID codec_id,
                          int width,
                          int height);

  /**
   * @brief 配置 AVCodecContext 使用硬件加速
   *
   * @param codec_ctx FFmpeg 解码器上下文
   * @return Result<void>
   */
  Result<void> ConfigureDecoder(AVCodecContext* codec_ctx);

#ifdef OS_WIN
  /**
   * @brief 从硬件帧获取 D3D11 纹理
   *
   * @param frame 硬件解码输出的 AVFrame
   * @return ID3D11Texture2D* (不拥有所有权)
   */
  ID3D11Texture2D* GetD3D11Texture(AVFrame* frame);

  /**
   * @brief 获取 D3D11 设备
   */
  ID3D11Device* GetD3D11Device() const;

  /**
   * @brief 获取 D3D11 设备上下文
   */
  ID3D11DeviceContext* GetD3D11DeviceContext() const;
#endif

  /**
   * @brief 是否已初始化
   */
  bool IsInitialized() const { return hw_device_ctx_ != nullptr; }

  /**
   * @brief 获取硬件解码器类型
   */
  HWDecoderType GetDecoderType() const { return decoder_type_; }

  /**
   * @brief 清理资源
   */
  void Cleanup();

 private:
  // FFmpeg 硬件格式选择回调
  static AVPixelFormat GetHWFormat(AVCodecContext* ctx,
                                   const AVPixelFormat* pix_fmts);

#ifdef OS_WIN
  Result<void> CreateD3D11VAContext();
  Result<void> CreateDXVA2Context();
#endif
  Result<void> CreateHWFramesContext(int width, int height);

  HWDecoderType decoder_type_ = HWDecoderType::kNone;
  AVBufferRef* hw_device_ctx_ = nullptr;  // AVHWDeviceContext
  AVBufferRef* hw_frames_ctx_ = nullptr;  // AVHWFramesContext
  AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;

#ifdef OS_WIN
  // D3D11 设备（从 AVHWDeviceContext 提取）
  ID3D11Device* d3d11_device_ = nullptr;
  ID3D11DeviceContext* d3d11_device_context_ = nullptr;
#endif
};

}  // namespace zenplay
