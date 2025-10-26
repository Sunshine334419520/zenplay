#include "player/video/render/impl/d3d11/d3d11_swap_chain.h"

#include "player/common/log_manager.h"
#include "player/common/win32_error_utils.h"

namespace zenplay {

D3D11SwapChain::~D3D11SwapChain() {
  Cleanup();
}

Result<void> D3D11SwapChain::Initialize(ID3D11Device* device,
                                        void* window_handle,
                                        int width,
                                        int height) {
  device_ = device;
  width_ = width;
  height_ = height;

  // 获取 DXGI Factory
  ComPtr<IDXGIDevice> dxgi_device;
  HRESULT hr = device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf()));
  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to query DXGI device");
  }

  ComPtr<IDXGIAdapter> dxgi_adapter;
  hr = dxgi_device->GetAdapter(dxgi_adapter.GetAddressOf());
  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to get DXGI adapter");
  }

  ComPtr<IDXGIFactory2> dxgi_factory;
  hr = dxgi_adapter->GetParent(IID_PPV_ARGS(dxgi_factory.GetAddressOf()));
  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to get DXGI factory");
  }

  // 配置交换链描�?
  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
  swap_chain_desc.Width = width;
  swap_chain_desc.Height = height;
  swap_chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // 标准 BGRA 格式
  swap_chain_desc.SampleDesc.Count = 1;                 // 无多重采�?
  swap_chain_desc.SampleDesc.Quality = 0;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.BufferCount = 2;                             // 双缓�?
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // Flip 模型
  swap_chain_desc.Flags = 0;

  // 创建交换�?
  hr = dxgi_factory->CreateSwapChainForHwnd(
      device, static_cast<HWND>(window_handle), &swap_chain_desc, nullptr,
      nullptr, swap_chain_.GetAddressOf());

  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to create swap chain");
  }

  // 禁用 Alt+Enter 全屏切换（由应用层控制）
  dxgi_factory->MakeWindowAssociation(static_cast<HWND>(window_handle),
                                      DXGI_MWA_NO_ALT_ENTER);

  // 创建渲染目标视图
  auto rtv_result = CreateRenderTargetView();
  if (!rtv_result.IsOk()) {
    return rtv_result;
  }

  MODULE_INFO(LOG_MODULE_RENDERER, "D3D11 swap chain created: {}x{}", width,
              height);
  return Result<void>::Ok();
}

Result<void> D3D11SwapChain::CreateRenderTargetView() {
  // 获取后台缓冲�?
  ComPtr<ID3D11Texture2D> back_buffer;
  HRESULT hr =
      swap_chain_->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to get swap chain back buffer");
  }

  // 创建渲染目标视图
  hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                       render_target_view_.GetAddressOf());
  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to create render target view");
  }

  return Result<void>::Ok();
}

Result<void> D3D11SwapChain::Resize(int width, int height) {
  if (width == width_ && height == height_) {
    return Result<void>::Ok();  // 尺寸未变�?
  }

  width_ = width;
  height_ = height;

  // 释放旧的渲染目标视图
  render_target_view_.Reset();

  // 调整交换链缓冲区大小
  HRESULT hr =
      swap_chain_->ResizeBuffers(2,  // 缓冲区数�?
                                 width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);

  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to resize swap chain buffers");
  }

  // 重新创建渲染目标视图
  auto rtv_result = CreateRenderTargetView();
  if (!rtv_result.IsOk()) {
    return rtv_result;
  }

  MODULE_INFO(LOG_MODULE_RENDERER, "Swap chain resized to {}x{}", width,
              height);
  return Result<void>::Ok();
}

void D3D11SwapChain::Present() {
  // 呈现帧：
  // 第一个参数：垂直同步间隔 (0 = 立即, 1 = 等待 VSync)
  // 第二个参数：呈现标志
  swap_chain_->Present(1, 0);  // 启用 VSync 减少撕裂
}

void D3D11SwapChain::Cleanup() {
  render_target_view_.Reset();
  swap_chain_.Reset();
  device_.Reset();
  MODULE_DEBUG(LOG_MODULE_RENDERER, "D3D11SwapChain cleaned up");
}

}  // namespace zenplay
