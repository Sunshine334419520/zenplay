#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "player/common/error.h"

namespace zenplay {

// 使用 Microsoft::WRL::ComPtr<T> 管理 COM 对象

/**
 * @brief D3D11 设备上下文管理
 *
 * 封装 D3D11 设备的创建、配置和生命周期管理
 */
class D3D11Context {
 public:
  D3D11Context() = default;
  ~D3D11Context();

  /**
   * @brief 初始化 D3D11 设备
   *
   * @param shared_device 共享的 D3D11 设备（来自硬件解码器，可为 nullptr）
   * @return Result<void>
   */
  Result<void> Initialize(ID3D11Device* shared_device = nullptr);

  /**
   * @brief 获取 D3D11 设备
   */
  ID3D11Device* GetDevice() const { return device_.Get(); }

  /**
   * @brief 获取 D3D11 设备上下文
   */
  ID3D11DeviceContext* GetDeviceContext() const {
    return device_context_.Get();
  }

  /**
   * @brief 获取功能级别
   */
  D3D_FEATURE_LEVEL GetFeatureLevel() const { return feature_level_; }

  /**
   * @brief 是否已初始化
   */
  bool IsInitialized() const { return device_ != nullptr; }

  /**
   * @brief 检查是否与解码器共享设备
   */
  bool IsSharedDevice() const { return is_shared_device_; }

  /**
   * @brief 清理资源
   */
  void Cleanup();

 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context_;
  D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_11_0;
  bool is_shared_device_ = false;  // 是否使用共享设备
};

}  // namespace zenplay
