#include "player/video/render/impl/d3d11/d3d11_context.h"

#include <dxgi.h>

#include "player/common/log_manager.h"
#include "player/common/win32_error_utils.h"

namespace zenplay {

D3D11Context::~D3D11Context() {
  Cleanup();
}

Result<void> D3D11Context::Initialize(ID3D11Device* shared_device) {
  // 如果提供了共享设备（来自硬件解码器），直接使用
  if (shared_device) {
    MODULE_INFO(LOG_MODULE_RENDERER, "Using shared D3D11 device from decoder");
    device_ = shared_device;
    device_->GetImmediateContext(device_context_.GetAddressOf());
    feature_level_ = device_->GetFeatureLevel();
    is_shared_device_ = true;
    return Result<void>::Ok();
  }

  // 创建新的 D3D11 设备
  MODULE_INFO(LOG_MODULE_RENDERER, "Creating new D3D11 device");

  // 支持的功能级别（从高到低）
  D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
  };

  UINT create_flags = 0;
#ifdef _DEBUG
  create_flags |= D3D11_CREATE_DEVICE_DEBUG;  // Debug 模式启用调试层
#endif

  HRESULT hr =
      D3D11CreateDevice(nullptr,                    // 默认适配器
                        D3D_DRIVER_TYPE_HARDWARE,   // 硬件加速
                        nullptr,                    // 软件栅格化器（不使用）
                        create_flags,               // 创建标志
                        feature_levels,             // 功能级别数组
                        ARRAYSIZE(feature_levels),  // 数组大小
                        D3D11_SDK_VERSION,          // SDK 版本
                        device_.GetAddressOf(),     // 输出设备
                        &feature_level_,            // 输出功能级别
                        device_context_.GetAddressOf());  // 输出设备上下文

  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to create D3D11 device");
  }

  MODULE_INFO(LOG_MODULE_RENDERER,
              "D3D11 device created, feature level: 0x{:x}",
              static_cast<int>(feature_level_));

  return Result<void>::Ok();
}

void D3D11Context::Cleanup() {
  if (!is_shared_device_) {
    // 只有自己创建的设备才需要释放
    device_context_.Reset();
    device_.Reset();
  } else {
    // 共享设备不释放，只清空指针
    device_context_.Reset();
    device_ = nullptr;
  }

  MODULE_DEBUG(LOG_MODULE_RENDERER, "D3D11Context cleaned up");
}

}  // namespace zenplay
