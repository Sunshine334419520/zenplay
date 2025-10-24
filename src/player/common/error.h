#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <utility>

namespace zenplay {

/**
 * @brief 统一的错误码枚举
 *
 * 用于跨模块的错误传播，支持区分可恢复错误与致命错误。
 * 范围规划：
 *   0-99: 通用错误
 *   100-199: 解封装/IO 相关错误
 *   200-299: 解码相关错误
 *   300-399: 渲染相关错误
 *   400-499: 音频相关错误
 *   500-599: 网络相关错误
 *   600-699: 同步相关错误
 *   700-799: 系统相关错误
 */
enum class ErrorCode : int {
  // 通用错误（0-99）
  kSuccess = 0,           ///< 成功
  kInvalidParameter = 1,  ///< 无效参数
  kNotInitialized = 2,    ///< 未初始化
  kAlreadyRunning = 3,    ///< 已在运行中
  kConfigError = 4,       ///< 配置错误
  kFileError = 5,         ///< 文件操作错误
  kUnknown = 99,          ///< 未知错误

  // 解封装/IO 错误（100-199）
  kIOError = 100,           ///< IO 错误（通用）
  kInvalidFormat = 101,     ///< 无效的文件格式
  kStreamNotFound = 102,    ///< 指定流未找到
  kDemuxError = 103,        ///< 解封装错误
  kFileNotFound = 104,      ///< 文件不存在
  kFileAccessDenied = 105,  ///< 文件访问被拒绝
  kEndOfFile = 106,         ///< 文件结束
  kDemuxerNotFound = 107,   ///< 解封装器未找到

  // 解码错误（200-299）
  kDecoderError = 200,               ///< 解码器错误
  kDecoderNotFound = 201,            ///< 未找到合适的解码器
  kUnsupportedCodec = 202,           ///< 不支持的编码格式
  kDecoderInitFailed = 203,          ///< 解码器初始化失败
  kDecoderSendFrameFailed = 204,     ///< 发送数据包到解码器失败
  kDecoderReceiveFrameFailed = 205,  ///< 从解码器接收帧失败
  kEncoderNotFound = 206,            ///< 编码器未找到

  // 渲染错误（300-399）
  kRenderError = 300,          ///< 渲染错误
  kInvalidRenderTarget = 301,  ///< 无效的渲染目标
  kRenderContextLost = 302,    ///< 渲染上下文丢失
  kTextureCreateFailed = 303,  ///< 纹理创建失败
  kRenderViewportError = 304,  ///< 视口设置错误

  // 音频错误（400-499）
  kAudioError = 400,               ///< 音频错误
  kAudioOutputError = 401,         ///< 音频输出错误
  kAudioFormatNotSupported = 402,  ///< 不支持的音频格式
  kAudioResampleError = 403,       ///< 重采样错误
  kAudioDeviceNotFound = 404,      ///< 音频设备未找到
  kAudioInitFailed = 405,          ///< 音频设备初始化失败
  kAudioNotInitialized = 406,      ///< 音频未初始化
  kAudioAlreadyInitialized = 407,  ///< 音频已初始化

  // 网络错误（500-599）
  kNetworkError = 500,        ///< 网络错误
  kConnectionTimeout = 501,   ///< 连接超时
  kConnectionRefused = 502,   ///< 连接被拒绝
  kInvalidUrl = 503,          ///< 无效的 URL
  kNetworkUnreachable = 504,  ///< 网络不可达
  kNetworkTimeout = 505,      ///< 网络超时

  // 同步相关错误（600-699）
  kSyncError = 600,   ///< 同步错误
  kClockError = 601,  ///< 时钟错误

  // 系统错误（700-799）
  kSystemError = 700,     ///< 系统错误
  kOutOfMemory = 701,     ///< 内存不足
  kThreadError = 702,     ///< 线程错误
  kTimeout = 703,         ///< 操作超时
  kInternalError = 704,   ///< 内部错误
  kBufferTooSmall = 705,  ///< 缓冲区太小
  kNotSupported = 706,    ///< 不支持的操作
};

/**
 * @brief 获取 ErrorCode 的可读字符串表示
 */
inline const char* ErrorCodeToString(ErrorCode code) {
  switch (code) {
    // 通用
    case ErrorCode::kSuccess:
      return "Success";
    case ErrorCode::kInvalidParameter:
      return "InvalidParameter";
    case ErrorCode::kNotInitialized:
      return "NotInitialized";
    case ErrorCode::kAlreadyRunning:
      return "AlreadyRunning";
    case ErrorCode::kConfigError:
      return "ConfigError";
    case ErrorCode::kFileError:
      return "FileError";
    case ErrorCode::kUnknown:
      return "Unknown";

    // 解封装/IO
    case ErrorCode::kIOError:
      return "IOError";
    case ErrorCode::kInvalidFormat:
      return "InvalidFormat";
    case ErrorCode::kStreamNotFound:
      return "StreamNotFound";
    case ErrorCode::kDemuxError:
      return "DemuxError";
    case ErrorCode::kFileNotFound:
      return "FileNotFound";
    case ErrorCode::kFileAccessDenied:
      return "FileAccessDenied";
    case ErrorCode::kEndOfFile:
      return "EndOfFile";
    case ErrorCode::kDemuxerNotFound:
      return "DemuxerNotFound";

    // 解码
    case ErrorCode::kDecoderError:
      return "DecoderError";
    case ErrorCode::kDecoderNotFound:
      return "DecoderNotFound";
    case ErrorCode::kUnsupportedCodec:
      return "UnsupportedCodec";
    case ErrorCode::kDecoderInitFailed:
      return "DecoderInitFailed";
    case ErrorCode::kDecoderSendFrameFailed:
      return "DecoderSendFrameFailed";
    case ErrorCode::kDecoderReceiveFrameFailed:
      return "DecoderReceiveFrameFailed";
    case ErrorCode::kEncoderNotFound:
      return "EncoderNotFound";

    // 渲染
    case ErrorCode::kRenderError:
      return "RenderError";
    case ErrorCode::kInvalidRenderTarget:
      return "InvalidRenderTarget";
    case ErrorCode::kRenderContextLost:
      return "RenderContextLost";
    case ErrorCode::kTextureCreateFailed:
      return "TextureCreateFailed";
    case ErrorCode::kRenderViewportError:
      return "RenderViewportError";

    // 音频
    case ErrorCode::kAudioError:
      return "AudioError";
    case ErrorCode::kAudioOutputError:
      return "AudioOutputError";
    case ErrorCode::kAudioFormatNotSupported:
      return "AudioFormatNotSupported";
    case ErrorCode::kAudioResampleError:
      return "AudioResampleError";
    case ErrorCode::kAudioDeviceNotFound:
      return "AudioDeviceNotFound";
    case ErrorCode::kAudioInitFailed:
      return "AudioInitFailed";
    case ErrorCode::kAudioNotInitialized:
      return "AudioNotInitialized";
    case ErrorCode::kAudioAlreadyInitialized:
      return "AudioAlreadyInitialized";

    // 网络
    case ErrorCode::kNetworkError:
      return "NetworkError";
    case ErrorCode::kConnectionTimeout:
      return "ConnectionTimeout";
    case ErrorCode::kConnectionRefused:
      return "ConnectionRefused";
    case ErrorCode::kInvalidUrl:
      return "InvalidUrl";
    case ErrorCode::kNetworkUnreachable:
      return "NetworkUnreachable";
    case ErrorCode::kNetworkTimeout:
      return "NetworkTimeout";

    // 同步
    case ErrorCode::kSyncError:
      return "SyncError";
    case ErrorCode::kClockError:
      return "ClockError";

    // 系统
    case ErrorCode::kSystemError:
      return "SystemError";
    case ErrorCode::kOutOfMemory:
      return "OutOfMemory";
    case ErrorCode::kThreadError:
      return "ThreadError";
    case ErrorCode::kTimeout:
      return "Timeout";
    case ErrorCode::kInternalError:
      return "InternalError";
    case ErrorCode::kBufferTooSmall:
      return "BufferTooSmall";
    case ErrorCode::kNotSupported:
      return "NotSupported";

    default:
      return "UnknownErrorCode";
  }
}

/**
 * @brief 统一的结果类型模板
 *
 * 使用示例：
 *   // 返回成功的值
 *   Result<int> r = Result<int>::Ok(42);
 *   if (r.IsOk()) { std::cout << r.Value(); }
 *
 *   // 返回错误
 *   Result<int> err = Result<int>::Err(ErrorCode::kInvalidParameter, "param
 * must > 0"); if (!err.IsOk()) { std::cout << "Error: " << err.Message();
 *   }
 *
 * 设计要点：
 *   1. 轻量级：仅包含必要的字段（value, error_code, message）
 *   2. 零开销：模板实例化，内联友好
 *   3. 移动语义：支持高效的所有权转移
 *   4. 便捷：提供便利方法（IsOk, IsErr, Err等）
 */
template <typename T>
class Result {
 public:
  using ValueType = T;

  /**
   * @brief 构造成功结果
   */
  static Result Ok(T value) {
    return Result(std::move(value), ErrorCode::kSuccess, std::string());
  }

  /**
   * @brief 构造失败结果
   */
  static Result Err(ErrorCode code, std::string message = std::string()) {
    return Result(T(), code, std::move(message));
  }

  /**
   * @brief 默认构造函数（创建未初始化的结果）
   * 通常不建议使用，仅为兼容性提供
   */
  Result() : error_code_(ErrorCode::kNotInitialized), message_() {}

  /**
   * @brief 移动构造函数
   */
  Result(Result&& other) noexcept
      : value_(std::move(other.value_)),
        error_code_(other.error_code_),
        message_(std::move(other.message_)) {}

  /**
   * @brief 移动赋值操作符
   */
  Result& operator=(Result&& other) noexcept {
    if (this != &other) {
      value_ = std::move(other.value_);
      error_code_ = other.error_code_;
      message_ = std::move(other.message_);
    }
    return *this;
  }

  /**
   * @brief 禁用拷贝构造和拷贝赋值（避免意外深拷贝）
   */
  Result(const Result&) = delete;
  Result& operator=(const Result&) = delete;

  ~Result() = default;

  // ============ 查询方法 ============

  /**
   * @brief 检查结果是否成功
   */
  bool IsOk() const { return error_code_ == ErrorCode::kSuccess; }

  /**
   * @brief 检查结果是否失败
   */
  bool IsErr() const { return error_code_ != ErrorCode::kSuccess; }

  /**
   * @brief 获取错误码
   */
  ErrorCode Code() const { return error_code_; }

  /**
   * @brief 获取错误消息
   */
  const std::string& Message() const { return message_; }

  /**
   * @brief 获取错误码的字符串表示
   */
  const char* CodeString() const { return ErrorCodeToString(error_code_); }

  // ============ 值访问方法 ============

  /**
   * @brief 获取值的可变引用
   * 注意：仅当 IsOk() 为 true 时才应调用此方法
   */
  T& Value() { return value_; }

  /**
   * @brief 获取值的不可变引用
   */
  const T& Value() const { return value_; }

  /**
   * @brief 获取值的所有权（移动）
   * 例如：std::unique_ptr 所有权转移
   * 注意：调用后对象中的值处于被移出状态
   */
  T TakeValue() { return std::move(value_); }

  /**
   * @brief 操作符重载：->
   * 允许直接访问值的成员（如果 T 是指针或有 operator*）
   */
  T* operator->() { return &value_; }

  const T* operator->() const { return &value_; }

  /**
   * @brief 操作符重载：*
   * 允许解引用（如果 T 是指针类型）
   */
  T& operator*() { return value_; }

  const T& operator*() const { return value_; }

  // ============ 链式操作 ============

  /**
   * @brief 如果当前是 Ok，应用函数返回新的 Result
   * 用于链式调用，便于函数组合
   *
   * 示例：
   *   Result<int> r = ParseInt("42")
   *     .AndThen([](int v) { return Result<int>::Ok(v * 2); });
   */
  template <typename F>
  auto AndThen(F&& f) -> std::invoke_result_t<F, T> {
    using ResultType = std::invoke_result_t<F, T>;
    if (!IsOk()) {
      return ResultType::Err(error_code_, message_);
    }
    return std::forward<F>(f)(std::move(value_));
  }

  /**
   * @brief 如果当前是 Err，应用错误处理函数返回新值
   * 用于错误恢复
   *
   * 示例：
   *   Result<int> r = OpenFile("missing.txt")
   *     .OrElse([](ErrorCode e) { return Result<int>::Ok(0); });
   */
  template <typename F>
  Result<T> OrElse(F&& f) {
    if (IsOk()) {
      return std::move(*this);
    }
    return std::forward<F>(f)(error_code_);
  }

  /**
   * @brief 如果当前是 Ok，应用映射函数到值，返回新的 Result<U>
   *
   * 示例：
   *   Result<std::string> r = Result<int>::Ok(42)
   *     .Map([](int v) { return std::to_string(v); });
   */
  template <typename F>
  Result<std::invoke_result_t<F, T>> Map(F&& f) {
    using ReturnType = std::invoke_result_t<F, T>;
    if (!IsOk()) {
      return Result<ReturnType>::Err(error_code_, message_);
    }
    return Result<ReturnType>::Ok(std::forward<F>(f)(std::move(value_)));
  }

  /**
   * @brief 如果当前是 Err，应用映射函数到错误码
   *
   * 示例：
   *   Result<int> r = SomeOperation()
   *     .MapErr([](ErrorCode e) { return ErrorCode::kUnknown; });
   */
  template <typename F>
  Result<T> MapErr(F&& f) {
    if (IsOk()) {
      return std::move(*this);
    }
    return Result<T>::Err(std::forward<F>(f)(error_code_), message_);
  }

  // ============ 便捷方法 ============

  /**
   * @brief 获取值，或在失败时返回默认值
   *
   * 示例：
   *   int value = result.ValueOr(0);
   */
  T ValueOr(T default_value) const {
    return IsOk() ? value_ : std::move(default_value);
  }

  /**
   * @brief 获取完整的错误消息字符串（包含错误码）
   *
   * 返回格式：[ErrorCodeString] message
   */
  std::string FullMessage() const {
    std::string full_msg = CodeString();
    if (!message_.empty()) {
      full_msg += ": ";
      full_msg += message_;
    }
    return full_msg;
  }

 private:
  // 私有构造函数
  Result(T value, ErrorCode code, std::string message)
      : value_(std::move(value)),
        error_code_(code),
        message_(std::move(message)) {}

  T value_;
  ErrorCode error_code_;
  std::string message_;
};

/**
 * @brief Result<void> 特化版本
 *
 * 当不需要返回值时使用，仅用于传递成功/失败状态和错误消息。
 * 使用示例：
 *   Result<void> Init() {
 *     if (!ValidateConfig()) {
 *       return Result<void>::Err(ErrorCode::kInvalidParameter, "config
 * invalid");
 *     }
 *     return Result<void>::Ok();
 *   }
 */
template <>
class Result<void> {
 public:
  /**
   * @brief 构造成功结果
   */
  static Result<void> Ok() {
    return Result<void>(ErrorCode::kSuccess, std::string());
  }

  /**
   * @brief 构造失败结果
   */
  static Result<void> Err(ErrorCode code, std::string message = std::string()) {
    return Result<void>(code, std::move(message));
  }

  /**
   * @brief 默认构造函数
   */
  Result<void>() : error_code_(ErrorCode::kNotInitialized), message_() {}

  /**
   * @brief 移动构造函数
   */
  Result<void>(Result&& other) noexcept
      : error_code_(other.error_code_), message_(std::move(other.message_)) {}

  /**
   * @brief 移动赋值操作符
   */
  Result<void>& operator=(Result<void>&& other) noexcept {
    if (this != &other) {
      error_code_ = other.error_code_;
      message_ = std::move(other.message_);
    }
    return *this;
  }

  Result<void>(const Result<void>&) = delete;
  Result<void>& operator=(const Result<void>&) = delete;

  ~Result<void>() = default;

  // ============ 查询方法 ============

  bool IsOk() const { return error_code_ == ErrorCode::kSuccess; }

  bool IsErr() const { return error_code_ != ErrorCode::kSuccess; }

  // 显式转换为 bool，用于测试断言 EXPECT_TRUE(result)
  explicit operator bool() const { return IsOk(); }

  ErrorCode Code() const { return error_code_; }

  const std::string& Message() const { return message_; }

  const char* CodeString() const { return ErrorCodeToString(error_code_); }

  // ============ 链式操作 ============

  /**
   * @brief 如果成功，执行给定的函数，并链式返回
   */
  template <typename F>
  Result<void> AndThen(F&& f) {
    if (!IsOk()) {
      return std::move(*this);
    }
    return std::forward<F>(f)();
  }

  /**
   * @brief 如果失败，执行错误处理并返回 Ok
   */
  template <typename F>
  Result<void> OrElse(F&& f) {
    if (IsOk()) {
      return std::move(*this);
    }
    std::forward<F>(f)(error_code_);
    return Result<void>::Ok();
  }

  /**
   * @brief 如果当前是 Err，应用映射函数到错误码
   *
   * 示例：
   *   Result<void> r = SomeOperation()
   *     .MapErr([](ErrorCode e) { return ErrorCode::kUnknown; });
   */
  template <typename F>
  Result<void> MapErr(F&& f) {
    if (IsOk()) {
      return std::move(*this);
    }
    return Result<void>::Err(std::forward<F>(f)(error_code_), message_);
  }

  /**
   * @brief 获取完整的错误消息字符串
   */
  std::string FullMessage() const {
    std::string full_msg = CodeString();
    if (!message_.empty()) {
      full_msg += ": ";
      full_msg += message_;
    }
    return full_msg;
  }

 private:
  explicit Result<void>(ErrorCode code, std::string message)
      : error_code_(code), message_(std::move(message)) {}

  ErrorCode error_code_;
  std::string message_;
};

/**
 * @brief 便捷类型别名
 */
using VoidResult = Result<void>;

/**
 * @brief 输出流操作符（便于调试和日志记录）
 */
template <typename T>
inline std::ostream& operator<<(std::ostream& os, const Result<T>& result) {
  os << result.FullMessage();
  return os;
}

}  // namespace zenplay
