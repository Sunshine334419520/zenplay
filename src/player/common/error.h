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
  OK = 0,               ///< 成功
  INVALID_PARAM = 1,    ///< 无效参数
  NOT_INITIALIZED = 2,  ///< 未初始化
  ALREADY_RUNNING = 3,  ///< 已在运行中
  UNKNOWN = 99,         ///< 未知错误

  // 解封装/IO 错误（100-199）
  IO_ERROR = 100,             ///< IO 错误（文件不存在、权限等）
  INVALID_FILE_FORMAT = 101,  ///< 无效的文件格式
  STREAM_NOT_FOUND = 102,     ///< 指定流未找到
  DEMUX_ERROR = 103,          ///< 解封装错误

  // 解码错误（200-299）
  DECODER_ERROR = 200,                 ///< 解码器错误
  DECODER_NOT_FOUND = 201,             ///< 未找到合适的解码器
  UNSUPPORTED_CODEC = 202,             ///< 不支持的编码格式
  DECODER_INIT_FAILED = 203,           ///< 解码器初始化失败
  DECODER_SEND_FRAME_FAILED = 204,     ///< 发送数据包到解码器失败
  DECODER_RECEIVE_FRAME_FAILED = 205,  ///< 从解码器接收帧失败

  // 渲染错误（300-399）
  RENDER_ERROR = 300,           ///< 渲染错误
  INVALID_RENDER_TARGET = 301,  ///< 无效的渲染目标
  RENDER_CONTEXT_LOST = 302,    ///< 渲染上下文丢失
  TEXTURE_CREATE_FAILED = 303,  ///< 纹理创建失败
  RENDER_VIEWPORT_ERROR = 304,  ///< 视口设置错误

  // 音频错误（400-499）
  AUDIO_ERROR = 400,                 ///< 音频错误
  AUDIO_OUTPUT_ERROR = 401,          ///< 音频输出错误
  AUDIO_FORMAT_NOT_SUPPORTED = 402,  ///< 不支持的音频格式
  AUDIO_RESAMPLE_ERROR = 403,        ///< 重采样错误
  AUDIO_DEVICE_NOT_FOUND = 404,      ///< 音频设备未找到
  AUDIO_DEVICE_INIT_FAILED = 405,    ///< 音频设备初始化失败

  // 网络错误（500-599）
  NETWORK_ERROR = 500,        ///< 网络错误
  CONNECTION_TIMEOUT = 501,   ///< 连接超时
  CONNECTION_REFUSED = 502,   ///< 连接被拒绝
  INVALID_URL = 503,          ///< 无效的 URL
  NETWORK_UNREACHABLE = 504,  ///< 网络不可达

  // 同步相关错误（600-699）
  SYNC_ERROR = 600,   ///< 同步错误
  CLOCK_ERROR = 601,  ///< 时钟错误

  // 系统错误（700-799）
  SYSTEM_ERROR = 700,   ///< 系统错误
  OUT_OF_MEMORY = 701,  ///< 内存不足
  THREAD_ERROR = 702,   ///< 线程错误
  TIMEOUT = 703,        ///< 操作超时
};

/**
 * @brief 获取 ErrorCode 的可读字符串表示
 */
inline const char* ErrorCodeToString(ErrorCode code) {
  switch (code) {
    // 通用
    case ErrorCode::OK:
      return "OK";
    case ErrorCode::INVALID_PARAM:
      return "InvalidParam";
    case ErrorCode::NOT_INITIALIZED:
      return "NotInitialized";
    case ErrorCode::ALREADY_RUNNING:
      return "AlreadyRunning";
    case ErrorCode::UNKNOWN:
      return "Unknown";

    // 解封装/IO
    case ErrorCode::IO_ERROR:
      return "IOError";
    case ErrorCode::INVALID_FILE_FORMAT:
      return "InvalidFileFormat";
    case ErrorCode::STREAM_NOT_FOUND:
      return "StreamNotFound";
    case ErrorCode::DEMUX_ERROR:
      return "DemuxError";

    // 解码
    case ErrorCode::DECODER_ERROR:
      return "DecoderError";
    case ErrorCode::DECODER_NOT_FOUND:
      return "DecoderNotFound";
    case ErrorCode::UNSUPPORTED_CODEC:
      return "UnsupportedCodec";
    case ErrorCode::DECODER_INIT_FAILED:
      return "DecoderInitFailed";
    case ErrorCode::DECODER_SEND_FRAME_FAILED:
      return "DecoderSendFrameFailed";
    case ErrorCode::DECODER_RECEIVE_FRAME_FAILED:
      return "DecoderReceiveFrameFailed";

    // 渲染
    case ErrorCode::RENDER_ERROR:
      return "RenderError";
    case ErrorCode::INVALID_RENDER_TARGET:
      return "InvalidRenderTarget";
    case ErrorCode::RENDER_CONTEXT_LOST:
      return "RenderContextLost";
    case ErrorCode::TEXTURE_CREATE_FAILED:
      return "TextureCreateFailed";
    case ErrorCode::RENDER_VIEWPORT_ERROR:
      return "RenderViewportError";

    // 音频
    case ErrorCode::AUDIO_ERROR:
      return "AudioError";
    case ErrorCode::AUDIO_OUTPUT_ERROR:
      return "AudioOutputError";
    case ErrorCode::AUDIO_FORMAT_NOT_SUPPORTED:
      return "AudioFormatNotSupported";
    case ErrorCode::AUDIO_RESAMPLE_ERROR:
      return "AudioResampleError";
    case ErrorCode::AUDIO_DEVICE_NOT_FOUND:
      return "AudioDeviceNotFound";
    case ErrorCode::AUDIO_DEVICE_INIT_FAILED:
      return "AudioDeviceInitFailed";

    // 网络
    case ErrorCode::NETWORK_ERROR:
      return "NetworkError";
    case ErrorCode::CONNECTION_TIMEOUT:
      return "ConnectionTimeout";
    case ErrorCode::CONNECTION_REFUSED:
      return "ConnectionRefused";
    case ErrorCode::INVALID_URL:
      return "InvalidURL";
    case ErrorCode::NETWORK_UNREACHABLE:
      return "NetworkUnreachable";

    // 同步
    case ErrorCode::SYNC_ERROR:
      return "SyncError";
    case ErrorCode::CLOCK_ERROR:
      return "ClockError";

    // 系统
    case ErrorCode::SYSTEM_ERROR:
      return "SystemError";
    case ErrorCode::OUT_OF_MEMORY:
      return "OutOfMemory";
    case ErrorCode::THREAD_ERROR:
      return "ThreadError";
    case ErrorCode::TIMEOUT:
      return "Timeout";

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
 *   Result<int> err = Result<int>::Err(ErrorCode::INVALID_PARAM, "param must >
 * 0"); if (!err.IsOk()) { std::cout << "Error: " << err.Message();
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
    return Result(std::move(value), ErrorCode::OK, std::string());
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
  Result() : error_code_(ErrorCode::NOT_INITIALIZED), message_() {}

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
  bool IsOk() const { return error_code_ == ErrorCode::OK; }

  /**
   * @brief 检查结果是否失败
   */
  bool IsErr() const { return error_code_ != ErrorCode::OK; }

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
   * @brief 如果当前是 Ok，对值应用函数并返回新的 Result
   * 用于链式调用，便于函数组合
   *
   * 示例：
   *   Result<int> r = ParseInt("42")
   *     .AndThen([](int v) { return Result<int>::Ok(v * 2); });
   */
  template <typename F>
  Result<typename std::invoke_result_t<F, T>::ValueType> AndThen(F&& f) {
    using ReturnType = typename std::invoke_result_t<F, T>::ValueType;
    if (!IsOk()) {
      return Result<ReturnType>::Err(error_code_, message_);
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
  Result<typename std::invoke_result_t<F, T>::type> Map(F&& f) {
    using ReturnType = typename std::invoke_result_t<F, T>::type;
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
   *     .MapErr([](ErrorCode e) { return ErrorCode::UNKNOWN; });
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
 *       return Result<void>::Err(ErrorCode::INVALID_PARAM, "config invalid");
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
    return Result<void>(ErrorCode::OK, std::string());
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
  Result<void>() : error_code_(ErrorCode::NOT_INITIALIZED), message_() {}

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

  bool IsOk() const { return error_code_ == ErrorCode::OK; }

  bool IsErr() const { return error_code_ != ErrorCode::OK; }

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
