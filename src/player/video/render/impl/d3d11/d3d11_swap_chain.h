#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "player/common/error.h"

namespace zenplay {

/**
 * @brief D3D11 交换链管理
 */
class D3D11SwapChain {
 public:
  D3D11SwapChain() = default;
  ~D3D11SwapChain();

  /**
   * @brief 初始化交换链
   *
   * @param device D3D11 设备
   * @param window_handle 窗口句柄
   * @param width 宽度
   * @param height 高度
   * @return Result<void>
   */
  Result<void> Initialize(ID3D11Device* device,
                          void* window_handle,
                          int width,
                          int height);

  /**
   * @brief 调整交换链大小
   */
  Result<void> Resize(int width, int height);

  /**
   * @brief 呈现帧到屏幕
   */
  void Present();

  /**
   * @brief 获取渲染目标视图
   */
  ID3D11RenderTargetView* GetRenderTargetView() const {
    return render_target_view_.Get();
  }

  /**
   * @brief 清理资源
   */
  void Cleanup();

 private:
  Result<void> CreateRenderTargetView();

  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view_;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;

  int width_ = 0;
  int height_ = 0;
};

}  // namespace zenplay
