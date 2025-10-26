#pragma once

// Windows 平台专用的 HRESULT 错误码映射工具
#ifdef OS_WIN

#include <windows.h>

#include <string>

#include "player/common/error.h"

namespace zenplay {

/**
 * @brief Windows HRESULT 错误码映射工具
 *
 * 将 Windows COM/WASAPI 的 HRESULT 错误码映射到 zenplay 的 ErrorCode 枚举。
 * 提供错误码转换和格式化功能。
 *
 * 使用示例：
 * @code
 * HRESULT hr = audio_client->Initialize();
 * if (FAILED(hr)) {
 *   return Result<void>::Err(MapHRESULT(hr));
 * }
 * @endcode
 */

/**
 * @brief 将 Windows HRESULT 映射到 ErrorCode
 *
 * @param hr Windows HRESULT 错误码
 * @return 对应的 ErrorCode
 *
 * 映射规则：
 * - S_OK → ErrorCode::kSuccess
 * - E_INVALIDARG → ErrorCode::kInvalidParameter
 * - E_OUTOFMEMORY → ErrorCode::kOutOfMemory
 * - AUDCLNT_E_DEVICE_INVALIDATED → ErrorCode::kAudioDeviceNotFound
 * - 其他 → ErrorCode::kAudioInitFailed
 */
ErrorCode MapHRESULT(HRESULT hr);

/**
 * @brief 格式化 HRESULT 错误消息
 *
 * @param hr Windows HRESULT 错误码
 * @param context 上下文信息（可选）
 * @return 格式化的错误消息
 *
 * 返回格式：
 * - "<context>: <error_message> (HRESULT: 0x<hex_value>)"
 *
 * 示例：
 * @code
 * FormatHRESULT(E_INVALIDARG, "Initialize audio client")
 * // 返回: "Initialize audio client: Invalid argument (HRESULT: 0x80070057)"
 * @endcode
 */
std::string FormatHRESULT(HRESULT hr, const std::string& context = "");

/**
 * @brief 创建带 HRESULT 错误信息的 Result
 *
 * @param hr Windows HRESULT 错误码
 * @param context 上下文信息（可选）
 * @return Result<void> 包含映射后的错误码和格式化消息
 *
 * 这是一个便利函数，组合了 MapHRESULT 和 FormatHRESULT。
 *
 * 示例：
 * @code
 * HRESULT hr = audio_client->Start();
 * if (FAILED(hr)) {
 *   return HRESULTToResult(hr, "Start audio client");
 * }
 * @endcode
 */
Result<void> HRESULTToResult(HRESULT hr, const std::string& context = "");

}  // namespace zenplay

#endif  // _WIN32
