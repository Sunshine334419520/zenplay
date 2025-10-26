#pragma once

#include <string>
#include <vector>

// Forward declarations for FFmpeg types
extern "C" {
enum AVHWDeviceType : int;
}

namespace zenplay {

/**
 * @brief 硬件解码器类型（跨平台）
 *
 * 设计原则：
 * 1. 平台无关的枚举名称
 * 2. 运行时检测可用性
 * 3. 便于添加新平台支持
 */
enum class HWDecoderType {
  kNone = 0,  // 软件解码（无硬件加速）

  // Windows 平台
  kD3D11VA,  // Windows 8+ D3D11 视频加速（推荐）
  kDXVA2,    // Windows 7+ DirectX 视频加速（兼容旧系统）

  // Linux 平台
  kVAAPI,  // Linux VA-API (Intel/AMD GPU)
  kVDPAU,  // Linux VDPAU (NVIDIA GPU)

  // macOS/iOS 平台
  kVideoToolbox,  // Apple VideoToolbox

  // 跨平台（理论上）
  kCUDA,    // NVIDIA CUDA (Windows/Linux)
  kOpenCL,  // OpenCL (实验性)
  kVulkan,  // Vulkan Video (新标准)
};

/**
 * @brief 硬件解码器信息
 */
struct HWDecoderInfo {
  HWDecoderType type;
  std::string name;         // 显示名称
  std::string description;  // 描述
  bool is_available;        // 当前系统是否可用
  int priority;             // 优先级（越大越优先）

  // 平台限制
  bool windows_only;
  bool linux_only;
  bool macos_only;
};

/**
 * @brief 硬件解码器类型工具类
 */
class HWDecoderTypeUtil {
 public:
  /**
   * @brief 获取解码器类型的名称
   */
  static std::string GetName(HWDecoderType type);

  /**
   * @brief 获取解码器类型的描述
   */
  static std::string GetDescription(HWDecoderType type);

  /**
   * @brief 判断当前平台是否支持该解码器类型
   *
   * 编译时检查：是否编译了该平台的代码
   * 运行时检查：系统是否真的支持（通过查询驱动/硬件）
   */
  static bool IsSupported(HWDecoderType type);

  /**
   * @brief 获取当前平台推荐的解码器类型列表（按优先级排序）
   */
  static std::vector<HWDecoderType> GetRecommendedTypes();

  /**
   * @brief 获取所有可用的解码器类型
   */
  static std::vector<HWDecoderInfo> GetAvailableDecoders();

  /**
   * @brief 转换为 FFmpeg AVHWDeviceType
   */
  static AVHWDeviceType ToFFmpegType(HWDecoderType type);
};

}  // namespace zenplay
