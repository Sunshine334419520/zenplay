#pragma once

#include <string>

#include "player/common/error.h"

extern "C" {
#include <libavutil/error.h>
}

namespace zenplay {

/**
 * @brief FFmpeg 错误码映射工具
 *
 * 将 FFmpeg 的 AVERROR 错误码映射到 zenplay 的 ErrorCode 枚举。
 * 提供错误码转换和格式化功能。
 *
 * 使用示例：
 * @code
 * int av_ret = avformat_open_input(&fmt_ctx, url.c_str(), nullptr, nullptr);
 * if (av_ret < 0) {
 *   return Result<void>::Err(MapFFmpegError(av_ret));
 * }
 * @endcode
 */

/**
 * @brief 将 FFmpeg 错误码映射到 ErrorCode
 *
 * @param av_error FFmpeg 的 AVERROR 错误码（负数）
 * @return 对应的 ErrorCode
 *
 * 映射规则：
 * - AVERROR_EOF → ErrorCode::kEndOfFile
 * - AVERROR(ENOENT) → ErrorCode::kFileNotFound
 * - AVERROR(ENOMEM) → ErrorCode::kOutOfMemory
 * - AVERROR_INVALIDDATA → ErrorCode::kInvalidFormat
 * - 其他 → ErrorCode::kDecoderError
 */
ErrorCode MapFFmpegError(int av_error);

/**
 * @brief 格式化 FFmpeg 错误消息
 *
 * @param av_error FFmpeg 的 AVERROR 错误码
 * @param context 上下文信息（可选，例如 "Open file"）
 * @return 格式化的错误消息
 *
 * 返回格式：
 * - 无上下文："FFmpeg error: <error_message> (code: <av_error>)"
 * - 有上下文："<context>: <error_message> (code: <av_error>)"
 *
 * 示例：
 * @code
 * FormatFFmpegError(-2, "Open file")
 * // 返回: "Open file: No such file or directory (code: -2)"
 * @endcode
 */
std::string FormatFFmpegError(int av_error, const std::string& context = "");

/**
 * @brief 创建带 FFmpeg 错误信息的 Result
 *
 * @param av_error FFmpeg 的 AVERROR 错误码
 * @param context 上下文信息（可选）
 * @return Result<void> 包含映射后的错误码和格式化消息
 *
 * 这是一个便利函数，组合了 MapFFmpegError 和 FormatFFmpegError。
 *
 * 示例：
 * @code
 * int ret = avformat_open_input(&fmt_ctx, url.c_str(), nullptr, nullptr);
 * if (ret < 0) {
 *   return FFmpegErrorToResult(ret, "Open input file");
 * }
 * @endcode
 */
Result<void> FFmpegErrorToResult(int av_error, const std::string& context = "");

}  // namespace zenplay
