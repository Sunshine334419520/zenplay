#include "player/common/ffmpeg_error_utils.h"

#include <sstream>

extern "C" {
#include <libavutil/error.h>
}

namespace zenplay {

ErrorCode MapFFmpegError(int av_error) {
  // FFmpeg 错误码是负数
  if (av_error >= 0) {
    return ErrorCode::kSuccess;
  }

  // 映射常见的 FFmpeg 错误码
  switch (av_error) {
    // 文件/流相关错误
    case AVERROR_EOF:
      return ErrorCode::kEndOfFile;

    case AVERROR(ENOENT):  // No such file or directory
      return ErrorCode::kFileNotFound;

    case AVERROR(EACCES):  // Permission denied
      return ErrorCode::kFileAccessDenied;

    case AVERROR(EINVAL):  // Invalid argument
      return ErrorCode::kInvalidParameter;

    // 内存相关错误
    case AVERROR(ENOMEM):  // Out of memory
      return ErrorCode::kOutOfMemory;

    // 格式相关错误
    case AVERROR_INVALIDDATA:  // Invalid data found
      return ErrorCode::kInvalidFormat;

    case AVERROR_DEMUXER_NOT_FOUND:
      return ErrorCode::kDemuxerNotFound;

    case AVERROR_DECODER_NOT_FOUND:
      return ErrorCode::kDecoderNotFound;

    case AVERROR_STREAM_NOT_FOUND:
      return ErrorCode::kStreamNotFound;

    case AVERROR_ENCODER_NOT_FOUND:
      return ErrorCode::kEncoderNotFound;

    // 协议/网络相关错误
    case AVERROR(ETIMEDOUT):  // Connection timed out
      return ErrorCode::kNetworkTimeout;

    case AVERROR(ECONNREFUSED):  // Connection refused
      return ErrorCode::kNetworkError;

    case AVERROR_PROTOCOL_NOT_FOUND:
      return ErrorCode::kNetworkError;

    // 解码相关错误
    case AVERROR(EAGAIN):  // Resource temporarily unavailable
      return ErrorCode::kDecoderError;

    case AVERROR_BUFFER_TOO_SMALL:
      return ErrorCode::kBufferTooSmall;

    case AVERROR_BUG:
      return ErrorCode::kInternalError;

    // 其他未知错误
    default:
      // 大多数未知的 FFmpeg 错误归为解码错误
      return ErrorCode::kDecoderError;
  }
}

std::string FormatFFmpegError(int av_error, const std::string& context) {
  // 获取 FFmpeg 的错误描述
  char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(av_error, errbuf, sizeof(errbuf));

  std::ostringstream oss;

  if (!context.empty()) {
    oss << context << ": ";
  } else {
    oss << "FFmpeg error: ";
  }

  oss << errbuf << " (code: " << av_error << ")";

  return oss.str();
}

Result<void> FFmpegErrorToResult(int av_error, const std::string& context) {
  if (av_error >= 0) {
    return Result<void>::Ok();
  }

  ErrorCode code = MapFFmpegError(av_error);
  std::string message = FormatFFmpegError(av_error, context);

  return Result<void>::Err(code, message);
}

}  // namespace zenplay
