#include <gtest/gtest.h>

#include "player/common/error.h"
#include "player/common/error_macros.h"
#include "player/common/ffmpeg_error_utils.h"

extern "C" {
#include <libavutil/error.h>
}

namespace zenplay {

// ============================================================================
// FFmpeg 错误码映射测试
// ============================================================================

TEST(FFmpegErrorUtilsTest, MapFFmpegError_Success) {
  EXPECT_EQ(MapFFmpegError(0), ErrorCode::kSuccess);
  EXPECT_EQ(MapFFmpegError(1), ErrorCode::kSuccess);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_EOF) {
  EXPECT_EQ(MapFFmpegError(AVERROR_EOF), ErrorCode::kEndOfFile);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_FileNotFound) {
  EXPECT_EQ(MapFFmpegError(AVERROR(ENOENT)), ErrorCode::kFileNotFound);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_AccessDenied) {
  EXPECT_EQ(MapFFmpegError(AVERROR(EACCES)), ErrorCode::kFileAccessDenied);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_InvalidParameter) {
  EXPECT_EQ(MapFFmpegError(AVERROR(EINVAL)), ErrorCode::kInvalidParameter);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_OutOfMemory) {
  EXPECT_EQ(MapFFmpegError(AVERROR(ENOMEM)), ErrorCode::kOutOfMemory);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_InvalidData) {
  EXPECT_EQ(MapFFmpegError(AVERROR_INVALIDDATA), ErrorCode::kInvalidFormat);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_DemuxerNotFound) {
  EXPECT_EQ(MapFFmpegError(AVERROR_DEMUXER_NOT_FOUND),
            ErrorCode::kDemuxerNotFound);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_DecoderNotFound) {
  EXPECT_EQ(MapFFmpegError(AVERROR_DECODER_NOT_FOUND),
            ErrorCode::kDecoderNotFound);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_StreamNotFound) {
  EXPECT_EQ(MapFFmpegError(AVERROR_STREAM_NOT_FOUND),
            ErrorCode::kStreamNotFound);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_NetworkTimeout) {
  EXPECT_EQ(MapFFmpegError(AVERROR(ETIMEDOUT)), ErrorCode::kNetworkTimeout);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_NetworkError) {
  EXPECT_EQ(MapFFmpegError(AVERROR(ECONNREFUSED)), ErrorCode::kNetworkError);
  EXPECT_EQ(MapFFmpegError(AVERROR_PROTOCOL_NOT_FOUND),
            ErrorCode::kNetworkError);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_DecoderError) {
  EXPECT_EQ(MapFFmpegError(AVERROR(EAGAIN)), ErrorCode::kDecoderError);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_BufferTooSmall) {
  EXPECT_EQ(MapFFmpegError(AVERROR_BUFFER_TOO_SMALL),
            ErrorCode::kBufferTooSmall);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_InternalError) {
  EXPECT_EQ(MapFFmpegError(AVERROR_BUG), ErrorCode::kInternalError);
}

TEST(FFmpegErrorUtilsTest, MapFFmpegError_UnknownError) {
  // 未知错误应该映射到 kDecoderError
  EXPECT_EQ(MapFFmpegError(-999999), ErrorCode::kDecoderError);
}

// ============================================================================
// FFmpeg 错误消息格式化测试
// ============================================================================

TEST(FFmpegErrorUtilsTest, FormatFFmpegError_WithoutContext) {
  std::string msg = FormatFFmpegError(AVERROR(ENOENT));
  EXPECT_NE(msg.find("FFmpeg error:"), std::string::npos);
  EXPECT_NE(msg.find("code:"), std::string::npos);
}

TEST(FFmpegErrorUtilsTest, FormatFFmpegError_WithContext) {
  std::string msg = FormatFFmpegError(AVERROR(ENOENT), "Open file");
  EXPECT_NE(msg.find("Open file:"), std::string::npos);
  EXPECT_NE(msg.find("code:"), std::string::npos);
  EXPECT_EQ(msg.find("FFmpeg error:"), std::string::npos);
}

TEST(FFmpegErrorUtilsTest, FormatFFmpegError_EOF) {
  std::string msg = FormatFFmpegError(AVERROR_EOF, "Read packet");
  EXPECT_NE(msg.find("Read packet:"), std::string::npos);
  EXPECT_NE(msg.find("End of file"), std::string::npos);
}

// ============================================================================
// FFmpegErrorToResult 测试
// ============================================================================

TEST(FFmpegErrorUtilsTest, FFmpegErrorToResult_Success) {
  auto result = FFmpegErrorToResult(0);
  EXPECT_TRUE(result);
  EXPECT_TRUE(result.IsOk());
}

TEST(FFmpegErrorUtilsTest, FFmpegErrorToResult_FileNotFound) {
  auto result = FFmpegErrorToResult(AVERROR(ENOENT), "Open input");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kFileNotFound);
  EXPECT_NE(result.Message().find("Open input:"), std::string::npos);
}

TEST(FFmpegErrorUtilsTest, FFmpegErrorToResult_EOF) {
  auto result = FFmpegErrorToResult(AVERROR_EOF, "Read frame");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kEndOfFile);
  EXPECT_NE(result.Message().find("Read frame:"), std::string::npos);
}

TEST(FFmpegErrorUtilsTest, FFmpegErrorToResult_InvalidData) {
  auto result = FFmpegErrorToResult(AVERROR_INVALIDDATA, "Parse stream");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kInvalidFormat);
}

// ============================================================================
// 宏测试
// ============================================================================

// 测试辅助函数
Result<void> HelperReturnSuccess() {
  return Result<void>::Ok();
}

Result<void> HelperReturnError() {
  return Result<void>::Err(ErrorCode::kFileNotFound, "Test error");
}

Result<int> HelperReturnValue(int value) {
  if (value < 0) {
    return Result<int>::Err(ErrorCode::kInvalidParameter, "Negative value");
  }
  return Result<int>::Ok(value);
}

TEST(ErrorMacrosTest, RETURN_IF_ERROR_Success) {
  auto test_fn = []() -> Result<void> {
    RETURN_IF_ERROR(HelperReturnSuccess());
    return Result<void>::Ok();
  };

  auto result = test_fn();
  EXPECT_TRUE(result);
}

TEST(ErrorMacrosTest, RETURN_IF_ERROR_Propagate) {
  auto test_fn = []() -> Result<void> {
    RETURN_IF_ERROR(HelperReturnError());
    return Result<void>::Ok();  // 不应该执行到这里
  };

  auto result = test_fn();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kFileNotFound);
}

TEST(ErrorMacrosTest, RETURN_IF_ERROR_WITH_AddContext) {
  auto test_fn = []() -> Result<void> {
    RETURN_IF_ERROR_WITH(HelperReturnError(), "Additional context");
    return Result<void>::Ok();
  };

  auto result = test_fn();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kFileNotFound);
  EXPECT_NE(result.Message().find("Additional context"),
            std::string::npos);
  EXPECT_NE(result.Message().find("Test error"), std::string::npos);
}

TEST(ErrorMacrosTest, BOOL_TO_RESULT_True) {
  auto test_fn = []() -> Result<void> {
    BOOL_TO_RESULT(true, ErrorCode::kInternalError, "Should not fail");
    return Result<void>::Ok();
  };

  auto result = test_fn();
  EXPECT_TRUE(result);
}

TEST(ErrorMacrosTest, BOOL_TO_RESULT_False) {
  auto test_fn = []() -> Result<void> {
    BOOL_TO_RESULT(false, ErrorCode::kInternalError, "Bool check failed");
    return Result<void>::Ok();
  };

  auto result = test_fn();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kInternalError);
  EXPECT_EQ(result.Message(), "Bool check failed");
}

TEST(ErrorMacrosTest, CHECK_NOT_NULL_NonNull) {
  auto test_fn = []() -> Result<void> {
    int dummy = 42;
    int* ptr = &dummy;
    CHECK_NOT_NULL(ptr, "Pointer should not be null");
    return Result<void>::Ok();
  };

  auto result = test_fn();
  EXPECT_TRUE(result);
}

TEST(ErrorMacrosTest, CHECK_NOT_NULL_Null) {
  auto test_fn = []() -> Result<void> {
    int* ptr = nullptr;
    CHECK_NOT_NULL(ptr, "Pointer is null");
    return Result<void>::Ok();
  };

  auto result = test_fn();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kInvalidParameter);
  EXPECT_EQ(result.Message(), "Pointer is null");
}

TEST(ErrorMacrosTest, ASSIGN_OR_RETURN_Success) {
  auto test_fn = []() -> Result<void> {
    ASSIGN_OR_RETURN(int value, HelperReturnValue(42));
    EXPECT_EQ(value, 42);
    return Result<void>::Ok();
  };

  auto result = test_fn();
  EXPECT_TRUE(result);
}

TEST(ErrorMacrosTest, ASSIGN_OR_RETURN_Error) {
  auto test_fn = []() -> Result<void> {
    ASSIGN_OR_RETURN(int value, HelperReturnValue(-1));
    (void)value;  // 不应该执行到这里
    return Result<void>::Ok();
  };

  auto result = test_fn();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kInvalidParameter);
}

// ============================================================================
// 集成测试：模拟真实场景
// ============================================================================

TEST(ErrorUtilsIntegrationTest, DemuxerOpenScenario) {
  auto simulate_demuxer_open = [](const std::string& url) -> Result<void> {
    // 模拟 avformat_open_input 失败
    int av_ret = AVERROR(ENOENT);  // 文件不存在
    if (av_ret < 0) {
      return FFmpegErrorToResult(av_ret, "Open input: " + url);
    }
    return Result<void>::Ok();
  };

  auto result = simulate_demuxer_open("non_existent_file.mp4");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kFileNotFound);
  EXPECT_NE(result.Message().find("Open input:"), std::string::npos);
  EXPECT_NE(result.Message().find("non_existent_file.mp4"),
            std::string::npos);
}

TEST(ErrorUtilsIntegrationTest, DecoderOpenScenario) {
  auto simulate_decoder_open = []() -> Result<void> {
    // 模拟 avcodec_open2 失败
    int av_ret = AVERROR_DECODER_NOT_FOUND;
    if (av_ret < 0) {
      return FFmpegErrorToResult(av_ret, "Open decoder");
    }
    return Result<void>::Ok();
  };

  auto result = simulate_decoder_open();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kDecoderNotFound);
}

TEST(ErrorUtilsIntegrationTest, MacroChainingScenario) {
  auto step1 = []() -> Result<void> { return Result<void>::Ok(); };
  auto step2 = []() -> Result<void> {
    return Result<void>::Err(ErrorCode::kNetworkTimeout, "Network timeout");
  };
  auto step3 = []() -> Result<void> { return Result<void>::Ok(); };

  auto workflow = [&]() -> Result<void> {
    RETURN_IF_ERROR(step1());
    RETURN_IF_ERROR(step2());  // 这里会失败
    RETURN_IF_ERROR(step3());  // 不应该执行到这里
    return Result<void>::Ok();
  };

  auto result = workflow();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kNetworkTimeout);
}

}  // namespace zenplay
