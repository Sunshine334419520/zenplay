#include "player/common/win32_error_utils.h"

#ifdef _WIN32

#include <audioclient.h>  // For AUDCLNT_E_* error codes
#include <comdef.h>       // For _com_error

#include <sstream>

namespace zenplay {

ErrorCode MapHRESULT(HRESULT hr) {
  // 成功
  if (SUCCEEDED(hr)) {
    return ErrorCode::kSuccess;
  }

  // 映射常见的 HRESULT 错误码
  switch (hr) {
    // 通用 COM 错误
    case E_INVALIDARG:
      return ErrorCode::kInvalidParameter;

    case E_OUTOFMEMORY:
      return ErrorCode::kOutOfMemory;

    case E_POINTER:
      return ErrorCode::kInvalidParameter;

    case E_HANDLE:
      return ErrorCode::kInvalidParameter;

    case E_NOTIMPL:
      return ErrorCode::kNotSupported;

    case E_FAIL:
      return ErrorCode::kInternalError;

    case E_ACCESSDENIED:
      return ErrorCode::kFileAccessDenied;

    // WASAPI 特定错误
    case AUDCLNT_E_NOT_INITIALIZED:
      return ErrorCode::kAudioNotInitialized;

    case AUDCLNT_E_ALREADY_INITIALIZED:
      return ErrorCode::kAudioAlreadyInitialized;

    case AUDCLNT_E_DEVICE_INVALIDATED:
    case AUDCLNT_E_DEVICE_IN_USE:
      return ErrorCode::kAudioDeviceNotFound;

    case AUDCLNT_E_UNSUPPORTED_FORMAT:
      return ErrorCode::kAudioFormatNotSupported;

    case AUDCLNT_E_BUFFER_TOO_LARGE:
    case AUDCLNT_E_BUFFER_SIZE_ERROR:
      return ErrorCode::kBufferTooSmall;

    case AUDCLNT_E_OUT_OF_ORDER:
    case AUDCLNT_E_WRONG_ENDPOINT_TYPE:
    case AUDCLNT_E_BUFFER_OPERATION_PENDING:
    case AUDCLNT_E_INVALID_DEVICE_PERIOD:
      return ErrorCode::kAudioOutputError;

    case AUDCLNT_E_SERVICE_NOT_RUNNING:
      return ErrorCode::kAudioDeviceNotFound;

    case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED:
      return ErrorCode::kAudioInitFailed;

    // 其他未知错误
    default:
      return ErrorCode::kAudioInitFailed;
  }
}

std::string FormatHRESULT(HRESULT hr, const std::string& context) {
  std::ostringstream oss;

  if (!context.empty()) {
    oss << context << ": ";
  } else {
    oss << "Windows error: ";
  }

  // 使用 _com_error 获取错误描述
  _com_error err(hr);
  LPCTSTR errMsg = err.ErrorMessage();

  if (errMsg) {
    // 转换 TCHAR* 到 std::string
#ifdef UNICODE
    // 如果是宽字符，需要转换
    int size = WideCharToMultiByte(CP_UTF8, 0, errMsg, -1, nullptr, 0, nullptr,
                                   nullptr);
    if (size > 0) {
      std::string str(size - 1, 0);
      WideCharToMultiByte(CP_UTF8, 0, errMsg, -1, &str[0], size, nullptr,
                          nullptr);
      oss << str;
    } else {
      oss << "Unknown error";
    }
#else
    oss << errMsg;
#endif
  } else {
    oss << "Unknown error";
  }

  oss << " (HRESULT: 0x" << std::hex << hr << ")";

  return oss.str();
}

Result<void> HRESULTToResult(HRESULT hr, const std::string& context) {
  if (SUCCEEDED(hr)) {
    return Result<void>::Ok();
  }

  ErrorCode code = MapHRESULT(hr);
  std::string message = FormatHRESULT(hr, context);

  return Result<void>::Err(code, message);
}

}  // namespace zenplay

#endif  // _WIN32
