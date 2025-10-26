#include "player/video/render/impl/d3d11/d3d11_shader.h"

#include <d3dcompiler.h>

#include "player/common/log_manager.h"
#include "player/common/win32_error_utils.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace zenplay {

// 嵌入式着色器源码
namespace ShaderSource {

// 顶点着色器源码
const char* VertexShaderSource = R"(
struct VSInput {
    uint vertexID : SV_VertexID;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float x = (input.vertexID & 1) ? 1.0 : -1.0;
    float y = (input.vertexID & 2) ? -1.0 : 1.0;
    output.position = float4(x, y, 0.0, 1.0);
    output.texcoord = float2((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return output;
}
)";

// 像素着色器源码（NV12 YUV到RGB转换）
const char* PixelShaderSource = R"(
Texture2D<float> yTexture : register(t0);
Texture2D<float2> uvTexture : register(t1);
SamplerState texSampler : register(s0);

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    float y = yTexture.Sample(texSampler, input.texcoord);
    float2 uv = uvTexture.Sample(texSampler, input.texcoord);
    float u = uv.x;
    float v = uv.y;
    
    // YUV 到 RGB 转换（BT.709 标准）
    y = (y - 0.0625) * 1.164;
    u = u - 0.5;
    v = v - 0.5;
    
    float r = y + 1.793 * v;
    float g = y - 0.213 * u - 0.533 * v;
    float b = y + 2.112 * u;
    
    return float4(saturate(r), saturate(g), saturate(b), 1.0);
}
)";

}  // namespace ShaderSource

D3D11Shader::~D3D11Shader() {
  Cleanup();
}

Result<void> D3D11Shader::Initialize(ID3D11Device* device) {
  auto vs_result = CreateVertexShader(device);
  if (!vs_result.IsOk()) {
    return vs_result;
  }

  auto ps_result = CreatePixelShader(device);
  if (!ps_result.IsOk()) {
    return ps_result;
  }

  auto layout_result = CreateInputLayout(device);
  if (!layout_result.IsOk()) {
    return layout_result;
  }

  auto sampler_result = CreateSamplerState(device);
  if (!sampler_result.IsOk()) {
    return sampler_result;
  }

  MODULE_INFO(LOG_MODULE_RENDERER, "D3D11 YUV→RGB shader initialized");
  return Result<void>::Ok();
}

Result<void> D3D11Shader::CreateVertexShader(ID3D11Device* device) {
  ComPtr<ID3DBlob> error_blob;

  HRESULT hr = D3DCompile(ShaderSource::VertexShaderSource,
                          strlen(ShaderSource::VertexShaderSource),
                          "VertexShader", nullptr, nullptr, "main", "vs_5_0",
                          D3DCOMPILE_ENABLE_STRICTNESS, 0,
                          vs_blob_.GetAddressOf(), error_blob.GetAddressOf());

  if (FAILED(hr)) {
    std::string error_msg = "Failed to compile vertex shader";
    if (error_blob) {
      error_msg += ": ";
      error_msg += static_cast<const char*>(error_blob->GetBufferPointer());
    }
    return HRESULTToResult(hr, error_msg);
  }

  hr = device->CreateVertexShader(vs_blob_->GetBufferPointer(),
                                  vs_blob_->GetBufferSize(), nullptr,
                                  vertex_shader_.GetAddressOf());

  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to create vertex shader");
  }

  return Result<void>::Ok();
}

Result<void> D3D11Shader::CreatePixelShader(ID3D11Device* device) {
  ComPtr<ID3DBlob> shader_blob;
  ComPtr<ID3DBlob> error_blob;

  HRESULT hr = D3DCompile(
      ShaderSource::PixelShaderSource, strlen(ShaderSource::PixelShaderSource),
      "PixelShader", nullptr, nullptr, "main", "ps_5_0",
      D3DCOMPILE_ENABLE_STRICTNESS, 0, shader_blob.GetAddressOf(),
      error_blob.GetAddressOf());

  if (FAILED(hr)) {
    std::string error_msg = "Failed to compile pixel shader";
    if (error_blob) {
      error_msg += ": ";
      error_msg += static_cast<const char*>(error_blob->GetBufferPointer());
    }
    return HRESULTToResult(hr, error_msg);
  }

  hr = device->CreatePixelShader(shader_blob->GetBufferPointer(),
                                 shader_blob->GetBufferSize(), nullptr,
                                 pixel_shader_.GetAddressOf());

  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to create pixel shader");
  }

  return Result<void>::Ok();
}

Result<void> D3D11Shader::CreateInputLayout(ID3D11Device* device) {
  // 顶点着色器使用 SV_VertexID，不需要实际的输入布局
  // 但 D3D11 要求至少有一个输入布局元素
  D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  HRESULT hr = device->CreateInputLayout(
      layout_desc, ARRAYSIZE(layout_desc), vs_blob_->GetBufferPointer(),
      vs_blob_->GetBufferSize(), input_layout_.GetAddressOf());

  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to create input layout");
  }

  return Result<void>::Ok();
}

Result<void> D3D11Shader::CreateSamplerState(ID3D11Device* device) {
  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

  HRESULT hr =
      device->CreateSamplerState(&sampler_desc, sampler_state_.GetAddressOf());
  if (FAILED(hr)) {
    return HRESULTToResult(hr, "Failed to create sampler state");
  }

  return Result<void>::Ok();
}

void D3D11Shader::Apply(ID3D11DeviceContext* device_context) {
  device_context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
  device_context->PSSetShader(pixel_shader_.Get(), nullptr, 0);
  device_context->IASetInputLayout(input_layout_.Get());
  device_context->PSSetSamplers(0, 1, sampler_state_.GetAddressOf());
}

void D3D11Shader::SetYUVTextures(ID3D11DeviceContext* device_context,
                                 ID3D11ShaderResourceView* y_texture,
                                 ID3D11ShaderResourceView* uv_texture) {
  ID3D11ShaderResourceView* textures[] = {y_texture, uv_texture};
  device_context->PSSetShaderResources(0, 2, textures);
}

void D3D11Shader::Cleanup() {
  vertex_shader_.Reset();
  pixel_shader_.Reset();
  input_layout_.Reset();
  sampler_state_.Reset();
  vs_blob_.Reset();
  MODULE_DEBUG(LOG_MODULE_RENDERER, "D3D11Shader cleaned up");
}

}  // namespace zenplay
