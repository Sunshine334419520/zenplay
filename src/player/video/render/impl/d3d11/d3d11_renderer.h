#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <vector>

#include "player/video/render/renderer.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace zenplay {

// 前置声明 - D3D11 组件类（只用到指针，不需要完整定义）
class D3D11Context;
class D3D11Shader;
class D3D11SwapChain;

/**
 * @brief D3D11 硬件加速渲染器
 *
 * 特性：
 * 1. 零拷贝渲染：直接使用硬件解码输出的 D3D11 纹理
 * 2. GPU YUV→RGB 转换：使用像素着色器转换
 * 3. 与解码器共享 D3D11 设备：避免跨设备传输
 */
class D3D11Renderer : public Renderer {
 public:
  D3D11Renderer();
  ~D3D11Renderer() override;

  /**
   * @brief 初始化渲染器
   */
  Result<void> Init(void* window_handle, int width, int height) override;

  /**
   * @brief 渲染一帧
   *
   * @param frame AVFrame（必须是 AV_PIX_FMT_D3D11 格式）
   */
  bool RenderFrame(AVFrame* frame) override;

  /**
   * @brief 清空渲染目标
   */
  void Clear() override;

  /**
   * @brief 呈现渲染结果到屏幕
   */
  void Present() override;

  /**
   * @brief 窗口大小变化
   */
  void OnResize(int width, int height) override;

  /**
   * @brief 清理资源
   */
  void Cleanup() override;

  /**
   * @brief 获取渲染器名称
   */
  const char* GetRendererName() const override;

  /**
   * @brief 清空所有 SRV 缓存（Seek 时调用）
   *
   * 防止 Seek 后使用野指针。具体来说：
   * - Seek 时 FFmpeg 释放旧的硬件纹理
   * - 如果不清空 SRV 缓存中的旧指针
   * - 新纹理恰好重用内存地址 → 野指针命中 → 崩溃
   */
  void ClearCaches() override;

  /**
   * @brief 设置共享的 D3D11 设备（来自硬件解码器）
   *
   * @param device 解码器使用的 D3D11 设备
   * @note 必须在 Init() 之前调用
   */
  void SetSharedD3D11Device(ID3D11Device* device);

 private:
  Result<void> CreateShaderResourceViews(AVFrame* frame);
  Result<void> RenderQuad();

  // 使用前置声明，减少头文件依赖
  std::unique_ptr<D3D11Context> d3d11_context_;
  std::unique_ptr<D3D11Shader> shader_;
  std::unique_ptr<D3D11SwapChain> swap_chain_;

  // Microsoft::WRL::ComPtr 需要完整类型定义，必须包含 d3d11.h
  // 纹理资源视图（用于着色器采样）
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> y_srv_;   // Y 平面
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uv_srv_;  // UV 平面（NV12）

  // 共享设备（来自解码器）- 原始指针可以使用前置声明
  ID3D11Device* shared_device_ = nullptr;

  // 🚀 SRV 池：为多个纹理缓存 SRV（性能关键！）
  // FFmpeg 使用纹理池（通常 4-16 个纹理），需要为每个纹理缓存对应的 SRV
  struct SRVCache {
    ID3D11Texture2D* texture;
    UINT array_slice;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> y_srv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uv_srv;
  };
  std::vector<SRVCache> srv_pool_;  // SRV 缓存池
  uint64_t srv_cache_hits_ = 0;     // 缓存命中次数
  uint64_t srv_cache_misses_ = 0;   // 缓存未命中次数

  int width_ = 0;
  int height_ = 0;
  bool initialized_ = false;
};

}  // namespace zenplay
