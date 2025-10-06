#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStyleFactory>
#include <QTextStream>

#include "loki/src/main_message_loop_with_not_main_thread.h"
#include "loki/src/threading/loki_thread.h"
#include "player/common/log_manager.h"
#include "player/stats/stats_initialization.h"
#include "view/main_window.h"

// Loki主消息循环代理
class ZenPlayMessageLoopDelegate : public loki::MainMessageLoop::Delegate {
 public:
  void OnSubThreadRegistry(
      std::vector<std::pair<loki::ID, std::string>>* subThreads) override {
    // 注册需要的子线程
    subThreads->push_back({loki::ID::UI, "UI Thread"});
    subThreads->push_back({loki::ID::IO, "IO Thread"});
  }
};

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  // 初始化日志系统
  // 编译期级别（CMakeLists.txt）：Debug=TRACE, Release=INFO
  // 运行期级别（这里设置）：Debug=DEBUG, Release=INFO
#ifdef NDEBUG
  // Release 模式：只输出 INFO 及以上
  if (!zenplay::LogManager::Initialize(zenplay::LogManager::LogLevel::INFO)) {
    return -1;
  }
#else
  // Debug 模式：输出 DEBUG 及以上（可以看到详细调试信息）
  if (!zenplay::LogManager::Initialize(zenplay::LogManager::LogLevel::DEBUG)) {
    return -1;
  }
#endif

  if (!zenplay::stats::InitializeStatsSystem()) {
    ZENPLAY_WARN("Continuing without statistics system");
  }

  ZENPLAY_INFO("Starting ZenPlay Media Player v1.0.0");

  // Set application properties
  app.setApplicationName("ZenPlay");
  app.setApplicationVersion("1.0.0");
  app.setOrganizationName("ZenPlay Team");
  app.setApplicationDisplayName("ZenPlay Media Player");

  // Set a modern dark style
  app.setStyle(QStyleFactory::create("Fusion"));

  // Apply dark theme
  QPalette darkPalette;
  darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
  darkPalette.setColor(QPalette::WindowText, Qt::white);
  darkPalette.setColor(QPalette::Base, QColor(26, 26, 26));
  darkPalette.setColor(QPalette::AlternateBase, QColor(64, 64, 64));
  darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
  darkPalette.setColor(QPalette::ToolTipText, Qt::white);
  darkPalette.setColor(QPalette::Text, Qt::white);
  darkPalette.setColor(QPalette::Button, QColor(45, 45, 45));
  darkPalette.setColor(QPalette::ButtonText, Qt::white);
  darkPalette.setColor(QPalette::BrightText, Qt::red);
  darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::HighlightedText, Qt::black);
  app.setPalette(darkPalette);

  // Load custom stylesheet
  QFile styleFile(":/styles/dark_theme.qss");
  if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
    QTextStream stream(&styleFile);
    QString style = stream.readAll();
    app.setStyleSheet(style);
  }

  // 初始化loki消息循环
  ZENPLAY_DEBUG("Initializing Loki message loop");
  ZenPlayMessageLoopDelegate delegate;
  loki::MainMessageLoopWithNotMainThread message_loop_with_not_main_thread(
      &delegate);
  message_loop_with_not_main_thread.Initialize();
  message_loop_with_not_main_thread.Run();

  // 创建主窗口并显示
  ZENPLAY_INFO("Creating main window");
  zenplay::MainWindow window;
  window.show();

  ZENPLAY_INFO("Application started successfully");
  int result = app.exec();

  ZENPLAY_INFO("Application exiting");
  zenplay::stats::ShutdownStatsSystem();
  zenplay::LogManager::Shutdown();

  return result;
}
