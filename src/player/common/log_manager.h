#pragma once

#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace zenplay {

class LogManager {
 public:
  enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERR = 4,
    CRITICAL = 5,
    OFF = 6
  };

  static bool Initialize(LogLevel log_level = LogLevel::INFO,
                         bool enable_file_log = true,
                         const std::string& log_file_path = "logs/zenplay.log",
                         size_t max_file_size = 1048576 * 5,
                         size_t max_files = 3);

  static void Shutdown();
  static std::shared_ptr<spdlog::logger> GetLogger();
  static std::shared_ptr<spdlog::logger> GetModuleLogger(
      const std::string& module_name);
  static void SetLogLevel(LogLevel level);

 private:
  static std::shared_ptr<spdlog::logger> main_logger_;
  static bool initialized_;
};

}  // namespace zenplay

// Convenience macros with source location
#define ZENPLAY_TRACE(...) \
  SPDLOG_LOGGER_TRACE(zenplay::LogManager::GetLogger(), __VA_ARGS__)
#define ZENPLAY_DEBUG(...) \
  SPDLOG_LOGGER_DEBUG(zenplay::LogManager::GetLogger(), __VA_ARGS__)
#define ZENPLAY_INFO(...) \
  SPDLOG_LOGGER_INFO(zenplay::LogManager::GetLogger(), __VA_ARGS__)
#define ZENPLAY_WARN(...) \
  SPDLOG_LOGGER_WARN(zenplay::LogManager::GetLogger(), __VA_ARGS__)
#define ZENPLAY_ERROR(...) \
  SPDLOG_LOGGER_ERROR(zenplay::LogManager::GetLogger(), __VA_ARGS__)
#define ZENPLAY_CRITICAL(...) \
  SPDLOG_LOGGER_CRITICAL(zenplay::LogManager::GetLogger(), __VA_ARGS__)

// Module specific log macros with source location
#define MODULE_TRACE(module, ...) \
  SPDLOG_LOGGER_TRACE(zenplay::LogManager::GetModuleLogger(module), __VA_ARGS__)
#define MODULE_DEBUG(module, ...) \
  SPDLOG_LOGGER_DEBUG(zenplay::LogManager::GetModuleLogger(module), __VA_ARGS__)
#define MODULE_INFO(module, ...) \
  SPDLOG_LOGGER_INFO(zenplay::LogManager::GetModuleLogger(module), __VA_ARGS__)
#define MODULE_WARN(module, ...) \
  SPDLOG_LOGGER_WARN(zenplay::LogManager::GetModuleLogger(module), __VA_ARGS__)
#define MODULE_ERROR(module, ...) \
  SPDLOG_LOGGER_ERROR(zenplay::LogManager::GetModuleLogger(module), __VA_ARGS__)
#define MODULE_CRITICAL(module, ...)                                   \
  SPDLOG_LOGGER_CRITICAL(zenplay::LogManager::GetModuleLogger(module), \
                         __VA_ARGS__)

// Module name constants
#define LOG_MODULE_PLAYER "Player"
#define LOG_MODULE_AUDIO "Audio"
#define LOG_MODULE_VIDEO "Video"
#define LOG_MODULE_DECODER "Decoder"
#define LOG_MODULE_DEMUXER "Demuxer"
#define LOG_MODULE_RENDERER "Renderer"
#define LOG_MODULE_SYNC "Sync"
