#include "d3d11_renderer.h"

#include <fmt/core.h>

#include "player/common/common_def.h"
#include "player/common/log_manager.h"
#include "player/video/render/impl/d3d11/d3d11_context.h"
#include "player/video/render/impl/d3d11/d3d11_shader.h"
#include "player/video/render/impl/d3d11/d3d11_swap_chain.h"

namespace zenplay {

D3D11Renderer::D3D11Renderer()
    : d3d11_context_(std::make_unique<D3D11Context>()),
      shader_(std::make_unique<D3D11Shader>()),
      swap_chain_(std::make_unique<D3D11SwapChain>()) {
  MODULE_INFO(LOG_MODULE_RENDERER, "D3D11Renderer created");
}

D3D11Renderer::~D3D11Renderer() {
  Cleanup();
}

void D3D11Renderer::SetSharedD3D11Device(ID3D11Device* device) {
  shared_device_ = device;
  MODULE_INFO(LOG_MODULE_RENDERER, "Shared D3D11 device set: {}",
              (void*)device);
}

Result<void> D3D11Renderer::Init(void* window_handle, int width, int height) {
  MODULE_INFO(LOG_MODULE_RENDERER, "Initializing D3D11Renderer ({}x{})", width,
              height);

  width_ = width;
  height_ = height;

  // 1. 初始化 D3D11 设备上下文
  auto context_result = d3d11_context_->Initialize(shared_device_);
  if (!context_result.IsOk()) {
    return context_result;
  }

  ID3D11Device* device = d3d11_context_->GetDevice();

  // 2. 初始化着色器
  auto shader_result = shader_->Initialize(device);
  if (!shader_result.IsOk()) {
    Cleanup();
    return shader_result;
  }

  // 3. 创建交换链
  auto swap_chain_result =
      swap_chain_->Initialize(device, window_handle, width, height);
  if (!swap_chain_result.IsOk()) {
    Cleanup();
    return swap_chain_result;
  }

  initialized_ = true;
  MODULE_INFO(LOG_MODULE_RENDERER, "D3D11Renderer initialized successfully");
  return Result<void>::Ok();
}

bool D3D11Renderer::RenderFrame(AVFrame* frame) {
  if (!initialized_) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "D3D11Renderer not initialized");
    return false;
  }

  if (!frame) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Frame is null");
    return false;
  }

  // 验证帧格式
  if (frame->format != AV_PIX_FMT_D3D11) {
    MODULE_ERROR(
        LOG_MODULE_RENDERER,
        "Frame format is not D3D11 (got {}), software rendering required",
        frame->format);
    return false;
  }

  // 🔑 零拷贝关键：从 AVFrame 提取 D3D11 纹理
  // frame->data[0] 存储的是 ID3D11Texture2D*
  // frame->data[1] 存储的是纹理数组索引（通常为 0）
  ID3D11Texture2D* decoded_texture =
      reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
  int texture_index =
      static_cast<int>(reinterpret_cast<intptr_t>(frame->data[1]));

  if (!decoded_texture) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to get D3D11 texture from frame");
    return false;
  }

  // 为 NV12 纹理创建着色器资源视图（如果尚未创建）
  auto srv_result = CreateShaderResourceViews(frame);
  if (!srv_result.IsOk()) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to create SRV: {}",
                 srv_result.FullMessage());
    return false;
  }

  // 清空渲染目标
  Clear();

  // 渲染全屏四边形
  auto render_result = RenderQuad();
  if (!render_result.IsOk()) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to render quad: {}",
                 render_result.FullMessage());
    return false;
  }

  // 呈现到屏幕
  Present();

  return true;
}

Result<void> D3D11Renderer::CreateShaderResourceViews(AVFrame* frame) {
  ID3D11Texture2D* texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);

  // 获取纹理描述
  D3D11_TEXTURE2D_DESC texture_desc;
  texture->GetDesc(&texture_desc);

  ID3D11Device* device = d3d11_context_->GetDevice();

  // NV12 格式：
  // - Y 平面：DXGI_FORMAT_R8_UNORM (单通道 8 位)
  // - UV 平面：DXGI_FORMAT_R8G8_UNORM (双通道 8 位，U 和 V 交织)

  // 创建 Y 平面的 SRV
  D3D11_SHADER_RESOURCE_VIEW_DESC y_srv_desc = {};
  y_srv_desc.Format = DXGI_FORMAT_R8_UNORM;
  y_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  y_srv_desc.Texture2D.MipLevels = 1;
  y_srv_desc.Texture2D.MostDetailedMip = 0;

  HRESULT hr = device->CreateShaderResourceView(
      texture, &y_srv_desc, y_srv_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("Failed to create Y plane SRV: HRESULT 0x{:08X}",
                    static_cast<uint32_t>(hr)));
  }

  // 创建 UV 平面的 SRV（色度子采样 4:2:0，宽高各为 Y 的一半）
  D3D11_SHADER_RESOURCE_VIEW_DESC uv_srv_desc = {};
  uv_srv_desc.Format = DXGI_FORMAT_R8G8_UNORM;
  uv_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  uv_srv_desc.Texture2D.MipLevels = 1;
  uv_srv_desc.Texture2D.MostDetailedMip = 0;

  hr = device->CreateShaderResourceView(texture, &uv_srv_desc,
                                        uv_srv_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("Failed to create UV plane SRV: HRESULT 0x{:08X}",
                    static_cast<uint32_t>(hr)));
  }

  MODULE_DEBUG(LOG_MODULE_RENDERER,
               "Shader resource views created for NV12 texture");
  return Result<void>::Ok();
}

Result<void> D3D11Renderer::RenderQuad() {
  ID3D11DeviceContext* device_context = d3d11_context_->GetDeviceContext();

  // 1. 设置渲染目标
  ID3D11RenderTargetView* rtv = swap_chain_->GetRenderTargetView();
  device_context->OMSetRenderTargets(1, &rtv, nullptr);

  // 2. 设置视口
  D3D11_VIEWPORT viewport = {};
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;
  viewport.Width = static_cast<float>(width_);
  viewport.Height = static_cast<float>(height_);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  device_context->RSSetViewports(1, &viewport);

  // 3. 应用着色器
  shader_->Apply(device_context);

  // 4. 绑定 YUV 纹理
  shader_->SetYUVTextures(device_context, y_srv_.Get(), uv_srv_.Get());

  // 5. 设置图元拓扑（三角形带）
  device_context->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  // 6. 绘制全屏四边形（4 个顶点，无索引缓冲）
  // 顶点着色器使用 SV_VertexID 生成顶点位置，无需顶点缓冲
  device_context->Draw(4, 0);

  return Result<void>::Ok();
}

void D3D11Renderer::Clear() {
  if (!initialized_) {
    return;
  }

  ID3D11DeviceContext* device_context = d3d11_context_->GetDeviceContext();
  ID3D11RenderTargetView* rtv = swap_chain_->GetRenderTargetView();

  // 清空渲染目标（黑色背景）
  float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  device_context->ClearRenderTargetView(rtv, clear_color);
}

void D3D11Renderer::Present() {
  if (!initialized_) {
    return;
  }

  swap_chain_->Present();
}

void D3D11Renderer::OnResize(int width, int height) {
  if (!initialized_) {
    return;
  }

  width_ = width;
  height_ = height;

  // 调整交换链大小
  auto result = swap_chain_->Resize(width, height);
  if (!result.IsOk()) {
    MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to resize swap chain: {}",
                 result.FullMessage());
  }
}

void D3D11Renderer::Cleanup() {
  if (!initialized_) {
    return;
  }

  MODULE_INFO(LOG_MODULE_RENDERER, "Cleaning up D3D11Renderer");

  y_srv_.Reset();
  uv_srv_.Reset();

  if (swap_chain_) {
    swap_chain_->Cleanup();
  }

  if (shader_) {
    shader_->Cleanup();
  }

  if (d3d11_context_) {
    d3d11_context_->Cleanup();
  }

  shared_device_ = nullptr;
  initialized_ = false;

  MODULE_INFO(LOG_MODULE_RENDERER, "D3D11Renderer cleaned up");
}

const char* D3D11Renderer::GetRendererName() const {
  return "D3D11 Hardware Renderer";
}

}  // namespace zenplay
