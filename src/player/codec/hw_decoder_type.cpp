#include "player/codec/hw_decoder_type.h"

extern "C" {
#include <libavutil/hwcontext.h>
}

namespace zenplay {

std::string HWDecoderTypeUtil::GetName(HWDecoderType type) {
  switch (type) {
    case HWDecoderType::kNone:
      return "Software";
    case HWDecoderType::kD3D11VA:
      return "D3D11VA";
    case HWDecoderType::kDXVA2:
      return "DXVA2";
    case HWDecoderType::kVAAPI:
      return "VA-API";
    case HWDecoderType::kVDPAU:
      return "VDPAU";
    case HWDecoderType::kVideoToolbox:
      return "VideoToolbox";
    case HWDecoderType::kCUDA:
      return "CUDA";
    case HWDecoderType::kOpenCL:
      return "OpenCL";
    case HWDecoderType::kVulkan:
      return "Vulkan";
    default:
      return "Unknown";
  }
}

std::string HWDecoderTypeUtil::GetDescription(HWDecoderType type) {
  switch (type) {
    case HWDecoderType::kNone:
      return "CPU software decoding (no hardware acceleration)";
    case HWDecoderType::kD3D11VA:
      return "Direct3D 11 Video Acceleration (Windows 8+, recommended)";
    case HWDecoderType::kDXVA2:
      return "DirectX Video Acceleration 2.0 (Windows 7+, legacy)";
    case HWDecoderType::kVAAPI:
      return "Video Acceleration API (Linux Intel/AMD GPUs)";
    case HWDecoderType::kVDPAU:
      return "Video Decode and Presentation API (Linux NVIDIA GPUs)";
    case HWDecoderType::kVideoToolbox:
      return "Apple VideoToolbox (macOS/iOS hardware acceleration)";
    case HWDecoderType::kCUDA:
      return "NVIDIA CUDA (cross-platform NVIDIA GPUs)";
    case HWDecoderType::kOpenCL:
      return "OpenCL (experimental, cross-platform)";
    case HWDecoderType::kVulkan:
      return "Vulkan Video (next-gen cross-platform API)";
    default:
      return "Unknown hardware decoder";
  }
}

bool HWDecoderTypeUtil::IsSupported(HWDecoderType type) {
  // 编译时检查：平台是否编译了对应代码
  switch (type) {
    case HWDecoderType::kNone:
      return true;  // 软件解码总是支持

#ifdef _WIN32
    case HWDecoderType::kD3D11VA:
    case HWDecoderType::kDXVA2:
      // Windows 平台编译时支持，但运行时还需检查系统版本
      return true;
#endif

#ifdef __linux__
    case HWDecoderType::kVAAPI:
    case HWDecoderType::kVDPAU:
      // Linux 平台编译时支持
      return true;
#endif

#ifdef __APPLE__
    case HWDecoderType::kVideoToolbox:
      // macOS/iOS 平台编译时支持
      return true;
#endif

    case HWDecoderType::kCUDA:
      // CUDA 支持需要编译时启用（通过 CMake 选项）
#ifdef ENABLE_CUDA
      return true;
#else
      return false;
#endif

    default:
      return false;
  }
}

std::vector<HWDecoderType> HWDecoderTypeUtil::GetRecommendedTypes() {
  std::vector<HWDecoderType> types;

#ifdef _WIN32
  // Windows: 优先 D3D11VA，备选 DXVA2
  types.push_back(HWDecoderType::kD3D11VA);
  types.push_back(HWDecoderType::kDXVA2);
#endif

#ifdef __linux__
  // Linux: 优先 VA-API (Intel/AMD)，备选 VDPAU (NVIDIA)
  types.push_back(HWDecoderType::kVAAPI);
  types.push_back(HWDecoderType::kVDPAU);
#endif

#ifdef __APPLE__
  // macOS/iOS: VideoToolbox
  types.push_back(HWDecoderType::kVideoToolbox);
#endif

  // 跨平台备选（如果编译时启用）
#ifdef ENABLE_CUDA
  types.push_back(HWDecoderType::kCUDA);
#endif

  // 软件解码作为最后的备选
  types.push_back(HWDecoderType::kNone);

  return types;
}

std::vector<HWDecoderInfo> HWDecoderTypeUtil::GetAvailableDecoders() {
  std::vector<HWDecoderInfo> decoders;

  // 定义所有解码器信息
  const HWDecoderInfo all_decoders[] = {
      {HWDecoderType::kNone, "Software", "CPU decoding", true, 0, false, false,
       false},
      {HWDecoderType::kD3D11VA, "D3D11VA", "Windows D3D11 acceleration", false,
       100, true, false, false},
      {HWDecoderType::kDXVA2, "DXVA2", "Windows DirectX acceleration", false,
       80, true, false, false},
      {HWDecoderType::kVAAPI, "VA-API", "Linux VA-API", false, 90, false, true,
       false},
      {HWDecoderType::kVDPAU, "VDPAU", "Linux VDPAU", false, 85, false, true,
       false},
      {HWDecoderType::kVideoToolbox, "VideoToolbox", "Apple acceleration",
       false, 95, false, false, true},
      {HWDecoderType::kCUDA, "CUDA", "NVIDIA CUDA", false, 70, false, false,
       false},
  };

  for (const auto& decoder : all_decoders) {
    HWDecoderInfo info = decoder;
    // 检查是否在当前平台可用
    info.is_available = IsSupported(info.type);
    decoders.push_back(info);
  }

  return decoders;
}

AVHWDeviceType HWDecoderTypeUtil::ToFFmpegType(HWDecoderType type) {
  switch (type) {
    case HWDecoderType::kD3D11VA:
      return AV_HWDEVICE_TYPE_D3D11VA;
    case HWDecoderType::kDXVA2:
      return AV_HWDEVICE_TYPE_DXVA2;
    case HWDecoderType::kVAAPI:
      return AV_HWDEVICE_TYPE_VAAPI;
    case HWDecoderType::kVDPAU:
      return AV_HWDEVICE_TYPE_VDPAU;
    case HWDecoderType::kVideoToolbox:
      return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
    case HWDecoderType::kCUDA:
      return AV_HWDEVICE_TYPE_CUDA;
    case HWDecoderType::kVulkan:
      return AV_HWDEVICE_TYPE_VULKAN;
    default:
      return AV_HWDEVICE_TYPE_NONE;
  }
}

}  // namespace zenplay
