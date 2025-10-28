#pragma once

#include <memory>
#include <string>

#include "player/codec/hw_decoder_type.h"

namespace zenplay {

// Forward declarations
class Renderer;
class GlobalConfig;
class HWDecoderContext;

/**
 * @brief 渲染路径选择结果
 */
struct RenderPathSelection {
  std::unique_ptr<Renderer>
      renderer;  // 创建的渲染器实例（已包装为 RendererProxy）
  std::unique_ptr<HWDecoderContext>
      hw_context;            // 硬件解码上下文（如果使用硬件加速）
  HWDecoderType hw_decoder;  // 推荐的硬件解码器类型
  std::string backend_name;  // 渲染后端名称（如 "D3D11", "SDL"）
  std::string reason;        // 选择原因（用于日志）
  bool is_hardware;          // 是否使用硬件加速

  RenderPathSelection()
      : hw_decoder(HWDecoderType::kNone),
        backend_name("Unknown"),
        is_hardware(false) {}
};

/**
 * @brief 渲染路径选择器（跨平台）
 *
 * 职责：
 * 1. 读取配置（从 GlobalConfig）
 * 2. 检测平台硬件能力（通过 HWDecoderTypeUtil）
 * 3. 创建硬件解码上下文（如果使用硬件加速）
 * 4. 创建对应的渲染器实现（共享硬件上下文）
 * 5. 处理回退逻辑
 *
 * 设计原则：
 * - 硬件解码和硬件渲染共享 D3D11 设备
 * - 平台无关的接口
 * - 配置驱动的决策
 * - 自动回退机制
 * - 详细的日志输出
 */
class RenderPathSelector {
 public:
  /**
   * @brief 选择并创建最佳的渲染器和硬件上下文
   *
   * @param codec_id 视频编解码器 ID（用于初始化硬件解码器）
   * @param width 视频宽度
   * @param height 视频高度
   * @param config 全局配置对象（如果为 nullptr，则使用默认配置）
   * @return RenderPathSelection 选择结果（包含渲染器和硬件上下文）
   *
   * @note 如果使用硬件加速，返回的 hw_context 和 renderer 共享同一个 D3D11 设备
   * @note 如果使用软件渲染，hw_context 为 nullptr
   * @note 返回的 renderer 已经包装为 RendererProxy
   */
  static RenderPathSelection Select(AVCodecID codec_id,
                                    int width,
                                    int height,
                                    GlobalConfig* config = nullptr);

  /**
   * @brief 创建默认软件渲染器（用于无视频流或纯音频场景）
   *
   * @return 软件渲染器（SDL，已包装为 RendererProxy）
   *
   * @note 这是一个简化接口，不需要视频流信息
   * @note 如果有视频流，应该使用 Select() 以获得硬件加速支持
   */
  static std::unique_ptr<Renderer> CreateDefaultRenderer();

 private:
 private:
  // 平台相关的选择函数
  static RenderPathSelection SelectForWindows(AVCodecID codec_id,
                                              int width,
                                              int height,
                                              GlobalConfig* config);
  static RenderPathSelection SelectForLinux(AVCodecID codec_id,
                                            int width,
                                            int height,
                                            GlobalConfig* config);
  static RenderPathSelection SelectForMacOS(AVCodecID codec_id,
                                            int width,
                                            int height,
                                            GlobalConfig* config);
  static RenderPathSelection SelectSoftwareFallback(const std::string& reason);

  // 辅助函数
  static bool IsHardwareAccelerationEnabled(GlobalConfig* config);
  static bool IsFallbackAllowed(GlobalConfig* config);
};

}  // namespace zenplay
