#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

#include "player/common/error.h"

namespace zenplay {

/**
 * @brief YUV 到 RGB 转换的 D3D11 着色器
 */
class D3D11Shader {
 public:
  D3D11Shader() = default;
  ~D3D11Shader();

  /**
   * @brief 初始化着色器
   *
   * @param device D3D11 设备
   * @return Result<void>
   */
  Result<void> Initialize(ID3D11Device* device);

  /**
   * @brief 应用着色器到渲染管线
   *
   * @param device_context D3D11 设备上下文
   */
  void Apply(ID3D11DeviceContext* device_context);

  /**
   * @brief 设置 YUV 纹理
   *
   * @param device_context D3D11 设备上下文
   * @param y_texture Y 平面纹理
   * @param uv_texture UV 平面纹理（NV12 格式）
   */
  void SetYUVTextures(ID3D11DeviceContext* device_context,
                      ID3D11ShaderResourceView* y_texture,
                      ID3D11ShaderResourceView* uv_texture);

  /**
   * @brief 清理资源
   */
  void Cleanup();

 private:
  Result<void> CreateVertexShader(ID3D11Device* device);
  Result<void> CreatePixelShader(ID3D11Device* device);
  Result<void> CreateInputLayout(ID3D11Device* device);
  Result<void> CreateSamplerState(ID3D11Device* device);

  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_state_;
  Microsoft::WRL::ComPtr<ID3DBlob>
      vs_blob_;  // 保存顶点着色器字节码用于输入布局
};

}  // namespace zenplay
