#include "log_manager.h"

#include <spdlog/async.h>
#include <spdlog/pattern_formatter.h>

#include <filesystem>
#include <iostream>

namespace zenplay {

std::shared_ptr<spdlog::logger> LogManager::main_logger_;
bool LogManager::initialized_ = false;

bool LogManager::Initialize(LogLevel log_level,
                            bool enable_file_log,
                            const std::string& log_file_path,
                            size_t max_file_size,
                            size_t max_files) {
  if (initialized_) {
    return true;
  }

  try {
    // 创建多个sink
    std::vector<spdlog::sink_ptr> sinks;

    // 控制台sink - 带颜色
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(static_cast<spdlog::level::level_enum>(log_level));

    // 设置控制台输出格式: [时间] [级别] [线程] [模块] 消息
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    sinks.push_back(console_sink);

    // 文件sink - 轮转日志
    if (enable_file_log) {
      // 确保日志目录存在
      std::filesystem::path log_path(log_file_path);
      std::filesystem::create_directories(log_path.parent_path());

      auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_file_path, max_file_size, max_files);
      file_sink->set_level(spdlog::level::trace);  // 文件记录所有级别

      // 设置文件输出格式: [时间] [级别] [线程] [模块] [文件:行号] 消息
      file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v [%s:%#]");
      sinks.push_back(file_sink);
    }

    // 创建主日志器
    main_logger_ =
        std::make_shared<spdlog::logger>("zenplay", sinks.begin(), sinks.end());
    main_logger_->set_level(static_cast<spdlog::level::level_enum>(log_level));
    main_logger_->flush_on(spdlog::level::warn);  // 警告及以上级别立即刷新

    // 注册为默认日志器
    spdlog::set_default_logger(main_logger_);

    // 设置全局刷新间隔
    spdlog::flush_every(std::chrono::seconds(3));

    initialized_ = true;

    ZENPLAY_INFO("Log system initialized successfully");
    ZENPLAY_INFO("Log level: {}",
                 spdlog::level::to_string_view(
                     static_cast<spdlog::level::level_enum>(log_level)));
    if (enable_file_log) {
      ZENPLAY_INFO("File logging enabled: {}", log_file_path);
    }

    return true;
  } catch (const std::exception& ex) {
    std::cerr << "Failed to initialize log system: " << ex.what() << std::endl;
    return false;
  }
}

void LogManager::Shutdown() {
  if (initialized_) {
    ZENPLAY_INFO("Shutting down log system");
    spdlog::shutdown();
    main_logger_.reset();
    initialized_ = false;
  }
}

std::shared_ptr<spdlog::logger> LogManager::GetLogger() {
  if (!initialized_) {
    // 如果未初始化，使用默认配置初始化
    Initialize();
  }
  return main_logger_;
}

std::shared_ptr<spdlog::logger> LogManager::GetModuleLogger(
    const std::string& module_name) {
  if (!initialized_) {
    Initialize();
  }

  // 检查是否已存在该模块的日志器
  auto logger = spdlog::get(module_name);
  if (logger) {
    return logger;
  }

  // 创建新的模块日志器，使用与主日志器相同的sink
  auto sinks = main_logger_->sinks();
  auto module_logger =
      std::make_shared<spdlog::logger>(module_name, sinks.begin(), sinks.end());
  module_logger->set_level(main_logger_->level());
  module_logger->flush_on(spdlog::level::warn);

  spdlog::register_logger(module_logger);
  return module_logger;
}

void LogManager::SetLogLevel(LogLevel level) {
  if (main_logger_) {
    auto spdlog_level = static_cast<spdlog::level::level_enum>(level);
    main_logger_->set_level(spdlog_level);
    spdlog::set_level(spdlog_level);

    ZENPLAY_INFO("Log level changed to: {}",
                 spdlog::level::to_string_view(spdlog_level));
  }
}

}  // namespace zenplay
