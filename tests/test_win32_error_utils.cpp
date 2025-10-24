#ifdef _WIN32

#include <windows.h>

#include <audioclient.h>
#include <gtest/gtest.h>

#include "player/common/error.h"
#include "player/common/win32_error_utils.h"

namespace zenplay {

// ============================================================================
// HRESULT 错误码映射测试
// ============================================================================

TEST(Win32ErrorUtilsTest, MapHRESULT_Success) {
  EXPECT_EQ(MapHRESULT(S_OK), ErrorCode::kSuccess);
  EXPECT_EQ(MapHRESULT(S_FALSE), ErrorCode::kSuccess);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_InvalidArg) {
  EXPECT_EQ(MapHRESULT(E_INVALIDARG), ErrorCode::kInvalidParameter);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_OutOfMemory) {
  EXPECT_EQ(MapHRESULT(E_OUTOFMEMORY), ErrorCode::kOutOfMemory);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_Pointer) {
  EXPECT_EQ(MapHRESULT(E_POINTER), ErrorCode::kInvalidParameter);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_NotImpl) {
  EXPECT_EQ(MapHRESULT(E_NOTIMPL), ErrorCode::kNotSupported);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_Fail) {
  EXPECT_EQ(MapHRESULT(E_FAIL), ErrorCode::kInternalError);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_AccessDenied) {
  EXPECT_EQ(MapHRESULT(E_ACCESSDENIED), ErrorCode::kFileAccessDenied);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_AudioNotInitialized) {
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_NOT_INITIALIZED),
            ErrorCode::kAudioNotInitialized);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_AudioAlreadyInitialized) {
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_ALREADY_INITIALIZED),
            ErrorCode::kAudioAlreadyInitialized);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_DeviceInvalidated) {
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_DEVICE_INVALIDATED),
            ErrorCode::kAudioDeviceNotFound);
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_DEVICE_IN_USE),
            ErrorCode::kAudioDeviceNotFound);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_UnsupportedFormat) {
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_UNSUPPORTED_FORMAT),
            ErrorCode::kAudioFormatNotSupported);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_BufferError) {
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_BUFFER_TOO_LARGE), ErrorCode::kBufferTooSmall);
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_BUFFER_SIZE_ERROR),
            ErrorCode::kBufferTooSmall);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_AudioOutputError) {
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_OUT_OF_ORDER), ErrorCode::kAudioOutputError);
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_WRONG_ENDPOINT_TYPE),
            ErrorCode::kAudioOutputError);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_ServiceNotRunning) {
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_SERVICE_NOT_RUNNING),
            ErrorCode::kAudioDeviceNotFound);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_ExclusiveModeNotAllowed) {
  EXPECT_EQ(MapHRESULT(AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED),
            ErrorCode::kAudioInitFailed);
}

TEST(Win32ErrorUtilsTest, MapHRESULT_UnknownError) {
  // 未知错误应该映射到 kAudioInitFailed
  HRESULT unknown_hr = 0x8888FFFF;
  EXPECT_EQ(MapHRESULT(unknown_hr), ErrorCode::kAudioInitFailed);
}

// ============================================================================
// HRESULT 错误消息格式化测试
// ============================================================================

TEST(Win32ErrorUtilsTest, FormatHRESULT_WithoutContext) {
  std::string msg = FormatHRESULT(E_INVALIDARG);
  EXPECT_NE(msg.find("Windows error:"), std::string::npos);
  EXPECT_NE(msg.find("HRESULT:"), std::string::npos);
  EXPECT_NE(msg.find("0x"), std::string::npos);
}

TEST(Win32ErrorUtilsTest, FormatHRESULT_WithContext) {
  std::string msg = FormatHRESULT(E_INVALIDARG, "Initialize audio");
  EXPECT_NE(msg.find("Initialize audio:"), std::string::npos);
  EXPECT_NE(msg.find("HRESULT:"), std::string::npos);
  EXPECT_EQ(msg.find("Windows error:"), std::string::npos);
}

TEST(Win32ErrorUtilsTest, FormatHRESULT_AudioError) {
  std::string msg =
      FormatHRESULT(AUDCLNT_E_DEVICE_INVALIDATED, "Start audio device");
  EXPECT_NE(msg.find("Start audio device:"), std::string::npos);
  EXPECT_NE(msg.find("HRESULT:"), std::string::npos);
}

// ============================================================================
// HRESULTToResult 测试
// ============================================================================

TEST(Win32ErrorUtilsTest, HRESULTToResult_Success) {
  auto result = HRESULTToResult(S_OK);
  EXPECT_TRUE(result);
  EXPECT_TRUE(result.IsOk());
}

TEST(Win32ErrorUtilsTest, HRESULTToResult_InvalidArg) {
  auto result = HRESULTToResult(E_INVALIDARG, "Parameter validation");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kInvalidParameter);
  EXPECT_NE(result.Message().find("Parameter validation:"), std::string::npos);
}

TEST(Win32ErrorUtilsTest, HRESULTToResult_AudioNotInitialized) {
  auto result = HRESULTToResult(AUDCLNT_E_NOT_INITIALIZED, "Start playback");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kAudioNotInitialized);
  EXPECT_NE(result.Message().find("Start playback:"), std::string::npos);
}

TEST(Win32ErrorUtilsTest, HRESULTToResult_DeviceInvalidated) {
  auto result =
      HRESULTToResult(AUDCLNT_E_DEVICE_INVALIDATED, "Audio device check");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kAudioDeviceNotFound);
}

// ============================================================================
// 集成测试：模拟 WASAPI 场景
// ============================================================================

TEST(Win32ErrorIntegrationTest, AudioInitializeScenario) {
  auto simulate_audio_init = []() -> Result<void> {
    // 模拟 IAudioClient::Initialize 失败
    HRESULT hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
    if (FAILED(hr)) {
      return HRESULTToResult(hr, "Initialize audio client");
    }
    return Result<void>::Ok();
  };

  auto result = simulate_audio_init();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kAudioFormatNotSupported);
  EXPECT_NE(result.Message().find("Initialize audio client:"),
            std::string::npos);
}

TEST(Win32ErrorIntegrationTest, AudioStartScenario) {
  auto simulate_audio_start = []() -> Result<void> {
    // 模拟 IAudioClient::Start 失败
    HRESULT hr = AUDCLNT_E_NOT_INITIALIZED;
    if (FAILED(hr)) {
      return HRESULTToResult(hr, "Start audio stream");
    }
    return Result<void>::Ok();
  };

  auto result = simulate_audio_start();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kAudioNotInitialized);
}

TEST(Win32ErrorIntegrationTest, DeviceEnumerationScenario) {
  auto simulate_device_enum = []() -> Result<void> {
    // 模拟设备枚举失败
    HRESULT hr = E_POINTER;
    if (FAILED(hr)) {
      return HRESULTToResult(hr, "Enumerate audio devices");
    }
    return Result<void>::Ok();
  };

  auto result = simulate_device_enum();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::kInvalidParameter);
}

}  // namespace zenplay

#endif  // _WIN32
