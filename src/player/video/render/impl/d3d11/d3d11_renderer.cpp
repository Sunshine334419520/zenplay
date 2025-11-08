#include "d3d11_renderer.h"

#include <fmt/core.h>

#include <cstdint>

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

  // 诊断：检查共享设备
  MODULE_INFO(LOG_MODULE_RENDERER, "🔍 Shared device before Init: {} ({})",
              (void*)shared_device_, shared_device_ ? "SET" : "NULL");

  width_ = width;
  height_ = height;

  // 1. 初始化 D3D11 设备上下文
  auto context_result = d3d11_context_->Initialize(shared_device_);
  if (!context_result.IsOk()) {
    return context_result;
  }

  ID3D11Device* device = d3d11_context_->GetDevice();

  // 诊断：验证设备是否相同
  MODULE_INFO(LOG_MODULE_RENDERER,
              "🔍 Device after context init: {}, same as shared: {}",
              (void*)device, device == shared_device_ ? "YES" : "NO");

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
  // frame->data[1] 存储的是纹理数组索引（NV12 纹理可能是数组资源）
  ID3D11Texture2D* decoded_texture =
      reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);

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
  const UINT array_slice =
      static_cast<UINT>(reinterpret_cast<uintptr_t>(frame->data[1]));

  // 🚀 性能优化：SRV 池 - 为 FFmpeg 纹理池中的每个纹理缓存 SRV
  // FFmpeg 通常使用 4-16 个纹理的池，需要为每个纹理维护对应的 SRV

  // 1. 先在池中查找是否已缓存
  for (auto& cache : srv_pool_) {
    if (cache.texture == texture && cache.array_slice == array_slice) {
      // 缓存命中：复用现有 SRV
      srv_cache_hits_++;
      y_srv_ = cache.y_srv;
      uv_srv_ = cache.uv_srv;

      // 每 100 次命中记录一次统计
      if (srv_cache_hits_ % 100 == 0) {
        MODULE_DEBUG(
            LOG_MODULE_RENDERER,
            "📊 SRV Pool: {} hits, {} misses, pool size: {} ({:.1f}% "
            "hit rate)",
            srv_cache_hits_, srv_cache_misses_, srv_pool_.size(),
            100.0 * srv_cache_hits_ / (srv_cache_hits_ + srv_cache_misses_));
      }
      return Result<void>::Ok();
    }
  }

  // 2. 缓存未命中：需要创建新的 SRV 并添加到池
  srv_cache_misses_++;

  MODULE_DEBUG(LOG_MODULE_RENDERER,
               "🔍 Creating NEW SRV (cache miss #{}): texture ptr = {}, "
               "slice = {}, pool size will be: {}",
               srv_cache_misses_, (void*)texture, array_slice,
               srv_pool_.size() + 1);

  ID3D11Device* device = d3d11_context_->GetDevice();

  D3D11_TEXTURE2D_DESC texture_desc;
  texture->GetDesc(&texture_desc);

  if (array_slice >= texture_desc.ArraySize) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("Invalid array slice {} for texture (ArraySize={})",
                    array_slice, texture_desc.ArraySize));
  }

  // 🔍 只在第一次验证设备和 BindFlags（避免每次缓存未命中都执行）
  if (srv_cache_misses_ == 1) {
    MODULE_INFO(LOG_MODULE_RENDERER,
                "🔍 First texture: format={}, size={}x{}, bind_flags=0x{:X}",
                static_cast<int>(texture_desc.Format), texture_desc.Width,
                texture_desc.Height, texture_desc.BindFlags);

    // 检查纹理来源设备
    Microsoft::WRL::ComPtr<ID3D11Device> texture_device;
    texture->GetDevice(texture_device.GetAddressOf());

    MODULE_INFO(LOG_MODULE_RENDERER,
                "🔍 Texture device: {}, Renderer device: {}, Match: {}",
                (void*)texture_device.Get(), (void*)device,
                texture_device.Get() == device ? "✅ YES" : "❌ NO");

    if (texture_device.Get() != device) {
      MODULE_ERROR(
          LOG_MODULE_RENDERER,
          "❌ Device mismatch! Texture was created on different D3D11 device. "
          "Zero-copy failed!");
      return Result<void>::Err(
          ErrorCode::kRenderError,
          "D3D11 device mismatch between decoder and renderer");
    }

    // 检查纹理绑定标志
    if (!(texture_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
      MODULE_ERROR(
          LOG_MODULE_RENDERER,
          "❌ ZERO-COPY FAILED: Texture missing D3D11_BIND_SHADER_RESOURCE "
          "flag!\n"
          "   Current BindFlags: 0x{:X}\n"
          "   Required: 0x{:X} (DECODER | SHADER_RESOURCE)\n"
          "   This means the hw_frames_ctx was not configured correctly.\n"
          "   Check HWDecoderContext::CreateCustomFramesContext()",
          texture_desc.BindFlags,
          D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE);
      return Result<void>::Err(
          ErrorCode::kRenderError,
          "D3D11 texture missing SHADER_RESOURCE bind flag");
    }

    MODULE_INFO(LOG_MODULE_RENDERER,
                "✅ Texture has correct BindFlags for zero-copy: 0x{:X}",
                texture_desc.BindFlags);
  }

  // 创建新的 SRV 缓存条目
  SRVCache new_cache;
  new_cache.texture = texture;
  new_cache.array_slice = array_slice;

  // 创建 Y 平面的 SRV
  D3D11_SHADER_RESOURCE_VIEW_DESC y_srv_desc = {};
  y_srv_desc.Format = DXGI_FORMAT_R8_UNORM;
  if (texture_desc.ArraySize > 1) {
    y_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    y_srv_desc.Texture2DArray.MostDetailedMip = 0;
    y_srv_desc.Texture2DArray.MipLevels = 1;
    y_srv_desc.Texture2DArray.FirstArraySlice = array_slice;
    y_srv_desc.Texture2DArray.ArraySize = 1;
  } else {
    y_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    y_srv_desc.Texture2D.MostDetailedMip = 0;
    y_srv_desc.Texture2D.MipLevels = 1;
  }

  HRESULT hr = device->CreateShaderResourceView(
      texture, &y_srv_desc, new_cache.y_srv.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("Failed to create Y plane SRV: HRESULT 0x{:08X}",
                    static_cast<uint32_t>(hr)));
  }

  // 创建 UV 平面的 SRV（色度子采样 4:2:0，宽高各为 Y 的一半）
  D3D11_SHADER_RESOURCE_VIEW_DESC uv_srv_desc = {};
  uv_srv_desc.Format = DXGI_FORMAT_R8G8_UNORM;
  if (texture_desc.ArraySize > 1) {
    uv_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    uv_srv_desc.Texture2DArray.MostDetailedMip = 0;
    uv_srv_desc.Texture2DArray.MipLevels = 1;
    uv_srv_desc.Texture2DArray.FirstArraySlice = array_slice;
    uv_srv_desc.Texture2DArray.ArraySize = 1;
  } else {
    uv_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    uv_srv_desc.Texture2D.MostDetailedMip = 0;
    uv_srv_desc.Texture2D.MipLevels = 1;
  }

  hr = device->CreateShaderResourceView(
      texture, &uv_srv_desc, new_cache.uv_srv.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("Failed to create UV plane SRV: HRESULT 0x{:08X}",
                    static_cast<uint32_t>(hr)));
  }

  // 添加到池并设置当前 SRV
  y_srv_ = new_cache.y_srv;
  uv_srv_ = new_cache.uv_srv;
  srv_pool_.push_back(std::move(new_cache));

  MODULE_DEBUG(LOG_MODULE_RENDERER,
               "✅ NEW SRV created and cached: texture {}, pool size now: {}",
               (void*)texture, srv_pool_.size());
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

  // 输出 SRV 缓存统计
  if (srv_cache_hits_ + srv_cache_misses_ > 0) {
    MODULE_INFO(
        LOG_MODULE_RENDERER,
        "📊 Final SRV Pool Stats: {} hits, {} misses, pool size: {} ({:.1f}% "
        "hit rate)",
        srv_cache_hits_, srv_cache_misses_, srv_pool_.size(),
        100.0 * srv_cache_hits_ / (srv_cache_hits_ + srv_cache_misses_));
  }

  y_srv_.Reset();
  uv_srv_.Reset();
  srv_pool_.clear();

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

void D3D11Renderer::ClearCaches() {
  MODULE_INFO(LOG_MODULE_RENDERER, "ClearCaches: starting cleanup");

  // ========================================
  // 1. 释放所有缓存的 SRV
  // ========================================
  for (auto& cache : srv_pool_) {
    if (cache.y_srv) {
      MODULE_DEBUG(LOG_MODULE_RENDERER, "Releasing Y SRV");
      cache.y_srv.Reset();
    }
    if (cache.uv_srv) {
      MODULE_DEBUG(LOG_MODULE_RENDERER, "Releasing UV SRV");
      cache.uv_srv.Reset();
    }
  }

  // ========================================
  // 2. 清空池
  // ========================================
  srv_pool_.clear();

  // ========================================
  // 3. 重置当前 SRV 指针
  // ========================================
  y_srv_.Reset();
  uv_srv_.Reset();

  // ========================================
  // 4. 重置统计计数
  // ========================================
  srv_cache_hits_ = 0;
  srv_cache_misses_ = 0;

  MODULE_INFO(LOG_MODULE_RENDERER, "✅ SRV caches cleared");
}

}  // namespace zenplay
