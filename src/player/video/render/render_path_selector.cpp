#include "render_path_selector.h"

#include <memory>

#include "impl/sdl/sdl_renderer.h"
#include "player/codec/hw_decoder_context.h"
#include "player/codec/hw_decoder_type.h"
#include "player/common/log_manager.h"
#include "player/config/global_config.h"
#include "renderer.h"
#include "renderer_proxy.h"

// 平台相关头文件
#ifdef _WIN32
#include "impl/d3d11/d3d11_renderer.h"
#endif

namespace zenplay {

// ==================== 公共接口 ====================

RenderPathSelection RenderPathSelector::Select(AVCodecID codec_id,
                                               int width,
                                               int height,
                                               GlobalConfig* config) {
  // 如果未提供配置，使用全局单例
  if (!config) {
    config = GlobalConfig::Instance();
  }

  // 根据平台选择
#ifdef _WIN32
  return SelectForWindows(codec_id, width, height, config);
#elif defined(__linux__)
  return SelectForLinux(codec_id, width, height, config);
#elif defined(__APPLE__)
  return SelectForMacOS(codec_id, width, height, config);
#else
  return SelectSoftwareFallback("Unsupported platform");
#endif
}

std::unique_ptr<Renderer> RenderPathSelector::CreateDefaultRenderer() {
  ZENPLAY_INFO(
      "Creating default software renderer (SDL, wrapped in RendererProxy)");

  // 创建 SDL 软件渲染器并包装为 RendererProxy
  auto sdl_renderer = std::make_unique<SDLRenderer>();
  return std::make_unique<RendererProxy>(std::move(sdl_renderer));
}

// ==================== Windows 平台 ====================
RenderPathSelection RenderPathSelector::SelectForWindows(AVCodecID codec_id,
                                                         int width,
                                                         int height,
                                                         GlobalConfig* config) {
  RenderPathSelection result;

  // 检查是否启用硬件加速
  if (!IsHardwareAccelerationEnabled(config)) {
    return SelectSoftwareFallback("Hardware acceleration disabled by config");
  }

  // 获取推荐的硬件解码器类型
  auto recommended_types = HWDecoderTypeUtil::GetRecommendedTypes();
  if (recommended_types.empty()) {
    return SelectSoftwareFallback("No hardware decoder available");
  }

  // 尝试 D3D11VA
  for (auto type : recommended_types) {
    if (type == HWDecoderType::kD3D11VA) {
      if (config->GetBool("render.hardware.allow_d3d11va", true)) {
        ZENPLAY_INFO(
            "Attempting to create D3D11 hardware acceleration pipeline");

        // 1. 创建硬件解码上下文
        auto hw_context = std::make_unique<HWDecoderContext>();
        auto init_result = hw_context->Initialize(HWDecoderType::kD3D11VA,
                                                  codec_id, width, height);

        if (init_result.IsOk()) {
          // 2. 创建 D3D11 渲染器
          auto d3d11_renderer = std::make_unique<D3D11Renderer>();

          // 3. 共享 D3D11 设备（零拷贝的关键）
          ID3D11Device* shared_device = hw_context->GetD3D11Device();
          if (shared_device) {
            d3d11_renderer->SetSharedD3D11Device(shared_device);
            ZENPLAY_INFO(
                "✅ D3D11 device shared between decoder and renderer "
                "(zero-copy enabled)");
          } else {
            ZENPLAY_INFO(
                "Failed to get D3D11 device from hw_context, zero-copy may not "
                "work");
          }

          // 4. 包装为 RendererProxy（确保在 loki UI 线程中执行）
          auto renderer_proxy =
              std::make_unique<RendererProxy>(std::move(d3d11_renderer));

          ZENPLAY_INFO(
              "✅ Selected D3D11 hardware acceleration (D3D11VA decoder + "
              "D3D11 renderer)");
          result.renderer = std::move(renderer_proxy);
          result.hw_context = std::move(hw_context);
          result.hw_decoder = HWDecoderType::kD3D11VA;
          result.backend_name = "D3D11";
          result.reason = "D3D11VA available and initialized successfully";
          result.is_hardware = true;
          return result;
        } else {
          ZENPLAY_WARN("Failed to initialize D3D11VA context: {}",
                       init_result.FullMessage());
          // 继续尝试其他选项或回退
        }
      } else {
        ZENPLAY_INFO("D3D11VA available but disabled by config");
      }
    } else if (type == HWDecoderType::kDXVA2) {
      if (config->GetBool("render.hardware.allow_dxva2", true)) {
        ZENPLAY_INFO("DXVA2 decoder available but not fully implemented yet");
        // TODO: 实现 DXVA2 完整支持
      }
    }
  }

  // 硬件加速不可用，检查是否允许回退
  if (IsFallbackAllowed(config)) {
    return SelectSoftwareFallback(
        "Hardware acceleration initialization failed on Windows");
  } else {
    ZENPLAY_ERROR("Hardware acceleration required but initialization failed");
    result.reason = "Hardware acceleration required but initialization failed";
    return result;  // renderer 为 nullptr
  }
}

// ==================== Linux 平台 ====================

RenderPathSelection RenderPathSelector::SelectForLinux(AVCodecID codec_id,
                                                       int width,
                                                       int height,
                                                       GlobalConfig* config) {
  RenderPathSelection result;

  // 检查是否启用硬件加速
  if (!IsHardwareAccelerationEnabled(config)) {
    return SelectSoftwareFallback("Hardware acceleration disabled by config");
  }

  // 获取推荐的硬件解码器类型
  auto recommended_types = HWDecoderTypeUtil::GetRecommendedTypes();
  if (recommended_types.empty()) {
    return SelectSoftwareFallback("No hardware decoder available");
  }

  // 检查 VAAPI/VDPAU
  for (auto type : recommended_types) {
    if (type == HWDecoderType::kVAAPI) {
      ZENPLAY_INFO(
          "VAAPI decoder available but hardware renderer not implemented yet");
      result.hw_decoder = type;
      // TODO: 实现 VaapiRenderer
      // auto hw_context = std::make_unique<HWDecoderContext>();
      // hw_context->Initialize(HWDecoderType::kVAAPI, codec_id, width, height);
    } else if (type == HWDecoderType::kVDPAU) {
      ZENPLAY_INFO(
          "VDPAU decoder available but hardware renderer not implemented yet");
      result.hw_decoder = type;
      // TODO: 实现 VdpauRenderer
    }
  }

  // Linux 硬件渲染暂未实现，回退到软件渲染
  if (IsFallbackAllowed(config)) {
    return SelectSoftwareFallback("Linux hardware renderer not implemented");
  } else {
    ZENPLAY_ERROR("Hardware acceleration required but not available on Linux");
    result.reason = "Hardware acceleration required but not implemented";
    return result;  // renderer 为 nullptr
  }
}

// ==================== macOS 平台 ====================

RenderPathSelection RenderPathSelector::SelectForMacOS(AVCodecID codec_id,
                                                       int width,
                                                       int height,
                                                       GlobalConfig* config) {
  RenderPathSelection result;

  // 检查是否启用硬件加速
  if (!IsHardwareAccelerationEnabled(config)) {
    return SelectSoftwareFallback("Hardware acceleration disabled by config");
  }

  // 获取推荐的硬件解码器类型
  auto recommended_types = HWDecoderTypeUtil::GetRecommendedTypes();
  if (recommended_types.empty()) {
    return SelectSoftwareFallback("No hardware decoder available");
  }

  // 检查 VideoToolbox
  for (auto type : recommended_types) {
    if (type == HWDecoderType::kVideoToolbox) {
      ZENPLAY_INFO(
          "VideoToolbox decoder available but hardware renderer not "
          "implemented yet");
      result.hw_decoder = type;
      // TODO: 实现 MetalRenderer
      // auto hw_context = std::make_unique<HWDecoderContext>();
      // hw_context->Initialize(HWDecoderType::kVideoToolbox, codec_id, width,
      // height);
    }
  }

  // macOS 硬件渲染暂未实现，回退到软件渲染
  if (IsFallbackAllowed(config)) {
    return SelectSoftwareFallback("macOS hardware renderer not implemented");
  } else {
    ZENPLAY_ERROR("Hardware acceleration required but not available on macOS");
    result.reason = "Hardware acceleration required but not implemented";
    return result;  // renderer 为 nullptr
  }
}

// ==================== 软件回退 ====================

RenderPathSelection RenderPathSelector::SelectSoftwareFallback(
    const std::string& reason) {
  ZENPLAY_INFO("Using SDL software renderer: {}", reason);

  RenderPathSelection result;

  // 创建 SDL 渲染器并包装为 RendererProxy
  auto sdl_renderer = std::make_unique<SDLRenderer>();
  result.renderer = std::make_unique<RendererProxy>(std::move(sdl_renderer));

  result.hw_context = nullptr;  // 软件渲染不需要硬件上下文
  result.hw_decoder = HWDecoderType::kNone;
  result.backend_name = "SDL";
  result.reason = reason;
  result.is_hardware = false;
  return result;
}

// ==================== 辅助函数 ====================

bool RenderPathSelector::IsHardwareAccelerationEnabled(GlobalConfig* config) {
  return config->GetBool("render.use_hardware_acceleration", false);
}

bool RenderPathSelector::IsFallbackAllowed(GlobalConfig* config) {
  return config->GetBool("render.hardware.allow_fallback", false);
}

}  // namespace zenplay
