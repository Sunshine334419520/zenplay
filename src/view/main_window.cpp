#include "main_window.h"

#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMessageBox>
#include <QMetaObject>
#include <QScreen>
#include <QUrl>
#include <QWindow>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
// #include <QWinTaskbarButton>  // Removed because it's not used and may not be
// available
#endif

#include "player/common/log_manager.h"
#include "player/common/player_state_manager.h"
#include "player/zen_player.h"

namespace zenplay {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      centralWidget_(nullptr),
      mainLayout_(nullptr),
      videoWidget_(nullptr),
      videoFrame_(nullptr),
      controlBar_(nullptr),
      controlLayout_(nullptr),
      playPauseBtn_(nullptr),
      stopBtn_(nullptr),
      timeLabel_(nullptr),
      progressSlider_(nullptr),
      durationLabel_(nullptr),
      volumeIcon_(nullptr),
      volumeSlider_(nullptr),
      fullscreenBtn_(nullptr),
      openFileAction_(nullptr),
      openUrlAction_(nullptr),
      exitAction_(nullptr),
      aboutAction_(nullptr),
      statusLabel_(nullptr),
      player_(std::make_unique<ZenPlayer>()),
      updateTimer_(new QTimer(this)),
      controlBarHideTimer_(nullptr),
      isDraggingProgress_(false),
      isFullscreen_(false),
      isControlBarVisible_(true),
      totalDuration_(0),
      state_callback_id_(-1),
      wasMaximized_(false) {
  setupUI();

  // ✅ 注册状态变更监听（用于异步 Seek）
  state_callback_id_ = player_->RegisterStateChangeCallback(
      [this](PlayerStateManager::PlayerState old_state,
             PlayerStateManager::PlayerState new_state) {
        // Qt 要求在主线程更新 UI，使用 QMetaObject::invokeMethod
        QMetaObject::invokeMethod(
            this,
            [this, old_state, new_state]() {
              handlePlayerStateChanged(old_state, new_state);
            },
            Qt::QueuedConnection);
      });

  // Connect timer for progress updates
  connect(updateTimer_, &QTimer::timeout, this,
          &MainWindow::updatePlaybackProgress);
  updateTimer_->setInterval(100);  // Update every 100ms

  // 初始化控制栏自动隐藏定时器（全屏时使用）
  controlBarHideTimer_ = new QTimer(this);
  controlBarHideTimer_->setSingleShot(true);
  connect(controlBarHideTimer_, &QTimer::timeout, this,
          &MainWindow::hideControlBar);

  // 安装事件过滤器以监听鼠标移动
  videoWidget_->installEventFilter(this);
  installEventFilter(this);

  // Set window properties
  setMinimumSize(800, 600);
  resize(1200, 800);
  setWindowTitle("ZenPlay Media Player");
  setWindowIcon(QIcon(":/icons/zenplay.png"));  // You'll need to add this icon
}

MainWindow::~MainWindow() {
  // ✅ 取消注册状态回调
  if (state_callback_id_ != -1 && player_) {
    player_->UnregisterStateChangeCallback(state_callback_id_);
    state_callback_id_ = -1;
  }
}

void MainWindow::setupUI() {
  setupMenuBar();

  centralWidget_ = new QWidget(this);
  setCentralWidget(centralWidget_);

  mainLayout_ = new QVBoxLayout(centralWidget_);
  mainLayout_->setContentsMargins(0, 0, 0, 0);
  mainLayout_->setSpacing(0);

  setupVideoArea();
  setupControlBar();
  setupStatusBar();

  updateControlBarState();
}

void MainWindow::setupMenuBar() {
  // File Menu
  QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

  openFileAction_ = new QAction(tr("&Open File..."), this);
  openFileAction_->setShortcut(QKeySequence::Open);
  openFileAction_->setStatusTip(tr("Open a media file"));
  connect(openFileAction_, &QAction::triggered, this,
          &MainWindow::openLocalFile);
  fileMenu->addAction(openFileAction_);

  openUrlAction_ = new QAction(tr("Open &URL..."), this);
  openUrlAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
  openUrlAction_->setStatusTip(tr("Open a network media URL"));
  connect(openUrlAction_, &QAction::triggered, this,
          &MainWindow::openNetworkUrl);
  fileMenu->addAction(openUrlAction_);

  fileMenu->addSeparator();

  exitAction_ = new QAction(tr("E&xit"), this);
  exitAction_->setShortcut(QKeySequence::Quit);
  exitAction_->setStatusTip(tr("Exit the application"));
  connect(exitAction_, &QAction::triggered, this, &QWidget::close);
  fileMenu->addAction(exitAction_);

  // Help Menu
  QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));

  aboutAction_ = new QAction(tr("&About ZenPlay"), this);
  aboutAction_->setStatusTip(tr("Show information about ZenPlay"));
  connect(aboutAction_, &QAction::triggered, [this]() {
    QMessageBox::about(
        this, tr("About ZenPlay"),
        tr("<h2>ZenPlay Media Player</h2>"
           "<p>Version 1.0.0</p>"
           "<p>A modern cross-platform media player built with Qt and "
           "FFmpeg.</p>"
           "<p>Powered by SDL2 for high-performance video rendering.</p>"));
  });
  helpMenu->addAction(aboutAction_);
}

void MainWindow::setupVideoArea() {
  videoFrame_ = new QFrame(this);
  videoFrame_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
  videoFrame_->setLineWidth(1);
  videoFrame_->setStyleSheet(
      "QFrame {"
      "    background-color: #1a1a1a;"
      "    border: 1px solid #404040;"
      "}");

  QVBoxLayout* videoLayout = new QVBoxLayout(videoFrame_);
  videoLayout->setContentsMargins(0, 0, 0, 0);

  videoWidget_ = new VideoDisplayWidget(videoFrame_);
  videoLayout->addWidget(videoWidget_);

  // Connect video widget signals
  connect(videoWidget_, &VideoDisplayWidget::doubleClicked, this,
          &MainWindow::toggleFullscreen);

  connect(videoWidget_, &VideoDisplayWidget::resized,
          [this](int width, int height) {
            // 通知渲染器窗口大小变化
            if (player_ && player_->IsOpened()) {
              player_->OnWindowResize(width, height);
            }
          });

  mainLayout_->addWidget(videoFrame_,
                         1);  // Give video area all available space
}

void MainWindow::setupControlBar() {
  controlBar_ = new QFrame(this);
  controlBar_->setFrameStyle(QFrame::NoFrame);
  controlBar_->setFixedHeight(60);
  controlBar_->setStyleSheet(
      "QFrame {"
      "    background-color: #2d2d2d;"
      "    border-top: 1px solid #404040;"
      "}"
      "QPushButton {"
      "    background-color: transparent;"
      "    border: none;"
      "    color: white;"
      "    font-size: 14px;"
      "    padding: 8px 12px;"
      "    border-radius: 4px;"
      "}"
      "QPushButton:hover {"
      "    background-color: #404040;"
      "}"
      "QPushButton:pressed {"
      "    background-color: #505050;"
      "}"
      "QSlider::groove:horizontal {"
      "    border: 1px solid #404040;"
      "    height: 4px;"
      "    background-color: #1a1a1a;"
      "    border-radius: 2px;"
      "}"
      "QSlider::handle:horizontal {"
      "    background-color: #ffffff;"
      "    border: 1px solid #404040;"
      "    width: 12px;"
      "    margin: -4px 0;"
      "    border-radius: 6px;"
      "}"
      "QSlider::handle:horizontal:hover {"
      "    background-color: #e0e0e0;"
      "}"
      "QSlider::sub-page:horizontal {"
      "    background-color: #0078d4;"
      "    border-radius: 2px;"
      "}"
      "QLabel {"
      "    color: #cccccc;"
      "    font-size: 12px;"
      "}");

  controlLayout_ = new QHBoxLayout(controlBar_);
  controlLayout_->setContentsMargins(12, 8, 12, 8);
  controlLayout_->setSpacing(8);

  // Play/Pause button
  playPauseBtn_ = new QPushButton("▶", this);
  playPauseBtn_->setFixedSize(40, 40);
  playPauseBtn_->setStyleSheet(playPauseBtn_->styleSheet() +
                               "font-size: 16px;");
  connect(playPauseBtn_, &QPushButton::clicked, this,
          &MainWindow::togglePlayPause);
  controlLayout_->addWidget(playPauseBtn_);

  // Stop button
  stopBtn_ = new QPushButton("⏹", this);
  stopBtn_->setFixedSize(40, 40);
  stopBtn_->setStyleSheet(stopBtn_->styleSheet() + "font-size: 14px;");
  connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::stopPlayback);
  controlLayout_->addWidget(stopBtn_);

  // Time label
  timeLabel_ = new QLabel("00:00", this);
  timeLabel_->setMinimumWidth(50);
  controlLayout_->addWidget(timeLabel_);

  // Progress slider
  progressSlider_ = new QSlider(Qt::Horizontal, this);
  progressSlider_->setMinimum(0);
  progressSlider_->setMaximum(100);
  progressSlider_->setValue(0);
  connect(progressSlider_, &QSlider::sliderPressed, this,
          &MainWindow::onProgressSliderPressed);
  connect(progressSlider_, &QSlider::sliderReleased, this,
          &MainWindow::onProgressSliderReleased);
  connect(progressSlider_, &QSlider::valueChanged, this,
          &MainWindow::onProgressSliderValueChanged);
  controlLayout_->addWidget(progressSlider_, 1);  // Take most of the space

  // Duration label
  durationLabel_ = new QLabel("00:00", this);
  durationLabel_->setMinimumWidth(50);
  controlLayout_->addWidget(durationLabel_);

  // Volume icon
  volumeIcon_ = new QLabel("🔊", this);
  volumeIcon_->setFixedSize(24, 24);
  volumeIcon_->setAlignment(Qt::AlignCenter);
  controlLayout_->addWidget(volumeIcon_);

  // Volume slider
  volumeSlider_ = new QSlider(Qt::Horizontal, this);
  volumeSlider_->setMinimum(0);
  volumeSlider_->setMaximum(100);
  volumeSlider_->setValue(50);
  volumeSlider_->setFixedWidth(80);
  connect(volumeSlider_, &QSlider::valueChanged, this,
          &MainWindow::onVolumeSliderValueChanged);
  controlLayout_->addWidget(volumeSlider_);

  // Fullscreen button
  fullscreenBtn_ = new QPushButton("⛶", this);
  fullscreenBtn_->setFixedSize(40, 40);
  fullscreenBtn_->setToolTip("全屏 (F11 / 双击视频)");
  connect(fullscreenBtn_, &QPushButton::clicked, this,
          &MainWindow::toggleFullscreen);
  controlLayout_->addWidget(fullscreenBtn_);

  mainLayout_->addWidget(controlBar_);
}

void MainWindow::setupStatusBar() {
  statusLabel_ = new QLabel("Ready", this);
  statusBar()->addWidget(statusLabel_);
  statusBar()->setStyleSheet(
      "QStatusBar {"
      "    background-color: #2d2d2d;"
      "    color: #cccccc;"
      "    border-top: 1px solid #404040;"
      "}");
}

void MainWindow::openLocalFile() {
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open Media File"), QString(),
      tr("Media Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v "
         "*.3gp *.mp3 *.wav *.flac *.aac *.ogg *.wma);;All Files (*.*)"));

  if (!fileName.isEmpty()) {
    setMediaFile(fileName);
  }
}

void MainWindow::openNetworkUrl() {
  bool ok;
  QString url = QInputDialog::getText(this, tr("Open Network URL"),
                                      tr("Enter media URL:"), QLineEdit::Normal,
                                      QString(), &ok);

  if (ok && !url.isEmpty()) {
    setMediaFile(url);
  }
}

void MainWindow::setMediaFile(const QString& filePath) {
  if (!player_) {
    return;
  }

  // Stop current playback and reset progress
  stopPlayback();
  progressSlider_->setValue(0);
  timeLabel_->setText(formatTime(0));  // 使用formatTime格式化0毫秒

  std::cout << "Opening media file: " << filePath.toStdString() << std::endl;

  // 尝试打开媒体文件
  if (!player_->Open(filePath.toStdString())) {
    statusLabel_->setText(tr("Failed to open media file"));
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to open media file:\n%1").arg(filePath));
    updateControlBarState();  // 更新UI状态反映打开失败
    return;
  }

  currentMediaPath_ = filePath;

  // 设置渲染窗口
  void* handle = videoWidget_->getNativeHandle();
  if (!handle || !player_->SetRenderWindow(handle, videoWidget_->width(),
                                           videoWidget_->height())) {
    statusLabel_->setText(tr("Failed to initialize renderer"));
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to initialize video renderer."));
    updateControlBarState();  // 更新UI状态反映渲染器失败
    return;
  }

  // 更新UI信息
  totalDuration_ = player_->GetDuration();  // 现在返回毫秒
  durationLabel_->setText(formatTime(totalDuration_));
  // 进度条使用秒为单位以避免int溢出
  progressSlider_->setMaximum(static_cast<int>(totalDuration_ / 1000));

  // 更新窗口标题
  QFileInfo fileInfo(filePath);
  setWindowTitle(tr("ZenPlay - %1").arg(fileInfo.fileName()));

  // 自动开始播放
  if (player_->Play()) {
    updateTimer_->start();
    statusLabel_->setText(tr("Playing"));
  } else {
    statusLabel_->setText(tr("Media loaded successfully"));
  }

  // 只在最后统一更新一次控制栏状态
  updateControlBarState();
}

void MainWindow::togglePlayPause() {
  if (!player_) {
    return;
  }

  bool success = false;
  QString statusText;

  using PlayerState = PlayerStateManager::PlayerState;
  switch (player_->GetState()) {
    case PlayerState::kIdle:
    case PlayerState::kStopped:
    case PlayerState::kPaused:
    case PlayerState::kOpening:
    case PlayerState::kBuffering:
    case PlayerState::kSeeking:
    case PlayerState::kError:
      success = player_->Play();
      if (success) {
        updateTimer_->start();
        statusText = tr("Playing");
      }
      break;
    case PlayerState::kPlaying:
      success = player_->Pause();
      if (success) {
        updateTimer_->stop();
        statusText = tr("Paused");
      }
      break;
  }

  if (success && !statusText.isEmpty()) {
    statusLabel_->setText(statusText);
  } else if (!success) {
    // 播放/暂停失败时的处理
    statusLabel_->setText(tr("Operation failed"));
  }

  updateControlBarState();
}

void MainWindow::stopPlayback() {
  if (!player_) {
    return;
  }

  if (player_->Stop()) {
    updateTimer_->stop();
    statusLabel_->setText(tr("Stopped"));

    // Reset progress
    progressSlider_->setValue(0);
    timeLabel_->setText(formatTime(0));  // 使用formatTime格式化0毫秒

    updateControlBarState();
  }
}

void MainWindow::onProgressSliderPressed() {
  isDraggingProgress_ = true;
}

void MainWindow::onProgressSliderReleased() {
  if (!player_ || !isDraggingProgress_) {
    return;
  }

  if (!player_->IsOpened()) {
    isDraggingProgress_ = false;
    return;
  }

  // 计算目标时间（秒转毫秒）
  int64_t seekTime = static_cast<int64_t>(progressSlider_->value()) * 1000;

  // ✅ 异步 Seek，立即返回，不阻塞 UI
  player_->SeekAsync(seekTime);

  // 注意：不在这里显示 "Seeking..." 状态
  // 状态更新由 handlePlayerStateChanged 回调处理

  isDraggingProgress_ = false;
}

void MainWindow::onProgressSliderValueChanged(int value) {
  if (!isDraggingProgress_) {
    return;
  }

  // Update time label while dragging - value是秒，需要转换为毫秒显示
  int64_t timeMs = static_cast<int64_t>(value) * 1000;
  timeLabel_->setText(formatTime(timeMs));
}

void MainWindow::onVolumeSliderValueChanged(int value) {
  // Update volume icon based on level
  if (value == 0) {
    volumeIcon_->setText("🔇");
  } else if (value < 30) {
    volumeIcon_->setText("🔉");
  } else {
    volumeIcon_->setText("🔊");
  }

  // TODO: Set actual volume in audio decoder
  statusLabel_->setText(tr("Volume: %1%").arg(value));
}

void MainWindow::updatePlaybackProgress() {
  if (!player_ || isDraggingProgress_) {
    return;
  }

  using PlayerState = PlayerStateManager::PlayerState;
  if (player_->GetState() == PlayerState::kPlaying) {
    // 获取真实播放时间（毫秒）
    int64_t currentTimeMs = player_->GetCurrentPlayTime();

    if (currentTimeMs <= totalDuration_) {
      updateProgressDisplay(currentTimeMs, totalDuration_);
    }
  }
}

void MainWindow::updateProgressDisplay(int64_t currentTimeMs,
                                       int64_t totalTimeMs) {
  if (!isDraggingProgress_ && progressSlider_ && timeLabel_) {
    // 进度条使用秒为单位以避免溢出
    int currentSeconds = static_cast<int>(currentTimeMs / 1000);
    progressSlider_->setValue(currentSeconds);
    timeLabel_->setText(formatTime(currentTimeMs));
  }
}

void MainWindow::updateControlBarState() {
  if (!player_ || !playPauseBtn_ || !stopBtn_ || !progressSlider_) {
    return;
  }

  bool hasMedia = player_->IsOpened();
  using PlayerState = PlayerStateManager::PlayerState;
  bool isPlaying = (player_->GetState() == PlayerState::kPlaying);

  // Update play/pause button
  if (isPlaying) {
    playPauseBtn_->setText("⏸");
    playPauseBtn_->setToolTip(tr("Pause"));
  } else {
    playPauseBtn_->setText("▶");
    playPauseBtn_->setToolTip(tr("Play"));
  }

  // Enable/disable controls based on media state
  playPauseBtn_->setEnabled(hasMedia);
  stopBtn_->setEnabled(hasMedia);
  progressSlider_->setEnabled(hasMedia);
}

QString MainWindow::formatTime(int64_t milliseconds) const {
  int64_t totalMs = milliseconds;
  int ms = totalMs % 1000;
  int totalSeconds = totalMs / 1000;
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int secs = totalSeconds % 60;

  if (hours > 0) {
    return QString("%1:%2:%3.%4")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
  } else {
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
  }
}

void MainWindow::onPlayerStateChanged() {
  updateControlBarState();
}

void MainWindow::handlePlayerStateChanged(
    PlayerStateManager::PlayerState old_state,
    PlayerStateManager::PlayerState new_state) {
  using State = PlayerStateManager::PlayerState;

  // 记录状态变化
  qDebug() << "Player state changed:"
           << PlayerStateManager::GetStateName(old_state) << "->"
           << PlayerStateManager::GetStateName(new_state);

  switch (new_state) {
    case State::kSeeking:
      // ✅ 显示 Seeking 状态
      statusLabel_->setText(tr("Seeking..."));
      statusLabel_->setStyleSheet("color: #FFA500;");  // 橙色

      // 禁用控制按钮，防止重复 Seek
      progressSlider_->setEnabled(false);
      playPauseBtn_->setEnabled(false);
      stopBtn_->setEnabled(false);

      // 显示等待光标
      setCursor(Qt::WaitCursor);
      break;

    case State::kPlaying:
      if (old_state == State::kSeeking) {
        // ✅ Seek 完成，恢复播放
        statusLabel_->setText(tr("Playing"));
        statusLabel_->setStyleSheet("color: #00FF00;");  // 绿色

        // 恢复控制按钮
        progressSlider_->setEnabled(true);
        playPauseBtn_->setEnabled(true);
        stopBtn_->setEnabled(true);

        // 恢复正常光标
        setCursor(Qt::ArrowCursor);
      } else {
        statusLabel_->setText(tr("Playing"));
        statusLabel_->setStyleSheet("color: #00FF00;");
      }

      playPauseBtn_->setText(tr("⏸ Pause"));

      if (!updateTimer_->isActive()) {
        updateTimer_->start();
      }
      break;

    case State::kPaused:
      if (old_state == State::kSeeking) {
        // ✅ Seek 完成，保持暂停
        statusLabel_->setText(tr("Paused"));
        statusLabel_->setStyleSheet("color: #FFFF00;");  // 黄色

        // 恢复控制按钮
        progressSlider_->setEnabled(true);
        playPauseBtn_->setEnabled(true);
        stopBtn_->setEnabled(true);

        // 恢复正常光标
        setCursor(Qt::ArrowCursor);

        // ✅ 关键修复：立即更新进度条到当前播放位置
        // Seek 完成后立即同步一次，避免显示旧值或 0
        int64_t currentTimeMs = player_->GetCurrentPlayTime();
        qDebug() << "Seek completed (paused), updating progress to:"
                 << currentTimeMs << "ms";
        updateProgressDisplay(currentTimeMs, totalDuration_);
      } else {
        statusLabel_->setText(tr("Paused"));
        statusLabel_->setStyleSheet("color: #FFFF00;");
      }

      playPauseBtn_->setText(tr("▶ Play"));
      updateTimer_->stop();
      break;

    case State::kStopped:
      statusLabel_->setText(tr("Stopped"));
      statusLabel_->setStyleSheet("color: #808080;");  // 灰色

      progressSlider_->setEnabled(true);
      playPauseBtn_->setEnabled(true);
      stopBtn_->setEnabled(true);
      setCursor(Qt::ArrowCursor);

      playPauseBtn_->setText(tr("▶ Play"));
      updateTimer_->stop();
      break;

    case State::kBuffering:
      statusLabel_->setText(tr("Buffering..."));
      statusLabel_->setStyleSheet("color: #00FFFF;");  // 青色
      break;

    case State::kError:
      if (old_state == State::kSeeking) {
        // ❌ Seek 失败
        QMessageBox::warning(
            this, tr("Seek Error"),
            tr("Failed to seek to the specified position. "
               "The file may not support seeking or the position is invalid."));
      }

      statusLabel_->setText(tr("Error"));
      statusLabel_->setStyleSheet("color: #FF0000;");  // 红色

      progressSlider_->setEnabled(true);
      playPauseBtn_->setEnabled(true);
      stopBtn_->setEnabled(true);
      setCursor(Qt::ArrowCursor);

      updateTimer_->stop();
      break;

    default:
      statusLabel_->setText(tr("Unknown State"));
      statusLabel_->setStyleSheet("color: #FFFFFF;");
      break;
  }

  // 更新控制栏状态
  updateControlBarState();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);

  // 通知 SDL 渲染器窗口大小变化
  if (player_ && videoWidget_ && player_->IsOpened()) {
    player_->OnWindowResize(videoWidget_->width(), videoWidget_->height());
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  stopPlayback();
  event->accept();
}

// VideoDisplayWidget Implementation
VideoDisplayWidget::VideoDisplayWidget(QWidget* parent)
    : QWidget(parent), placeholderLabel_(nullptr) {
  setMinimumSize(320, 240);
  setStyleSheet("background-color: black;");

  // Create placeholder
  placeholderLabel_ = new QLabel(this);
  placeholderLabel_->setText(
      "<div style='text-align: center; color: #888888;'>"
      "<p style='font-size: 48px; margin: 20px;'>🎬</p>"
      "<p style='font-size: 16px;'>No Media Loaded</p>"
      "<p style='font-size: 12px;'>Open a file to start playing</p>"
      "</div>");
  placeholderLabel_->setAlignment(Qt::AlignCenter);
  placeholderLabel_->setWordWrap(true);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->addWidget(placeholderLabel_);
}

void* VideoDisplayWidget::getNativeHandle() {
#ifdef _WIN32
  return reinterpret_cast<void*>(winId());
#else
  // For other platforms, you might need different handling
  return reinterpret_cast<void*>(winId());
#endif
}

void VideoDisplayWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event)
  // SDL will handle the actual rendering
  // This is just for the widget background
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);
}

void VideoDisplayWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  emit resized(event->size().width(), event->size().height());
}

void VideoDisplayWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  Q_UNUSED(event);
  emit doubleClicked();
}

// ============ 全屏功能实现 ============

void MainWindow::toggleFullscreen() {
  if (isFullscreen_) {
    exitFullscreen();
  } else {
    enterFullscreen();
  }
}

void MainWindow::enterFullscreen() {
  // 保存当前窗口状态
  if (!isFullscreen_) {
    wasMaximized_ = isMaximized();
    if (!wasMaximized_) {
      normalGeometry_ = geometry();
    }
  }

  isFullscreen_ = true;
  isControlBarVisible_ = true;  // 初始显示控制栏

  // 隐藏菜单栏和状态栏
  menuBar()->hide();
  statusBar()->hide();

  // 进入全屏
  showFullScreen();

  // 更新全屏按钮图标
  updateFullscreenButton();

  // 启动控制栏自动隐藏定时器（3秒后隐藏）
  controlBarHideTimer_->start(3000);
}

void MainWindow::exitFullscreen() {
  isFullscreen_ = false;
  isControlBarVisible_ = true;

  // 停止控制栏隐藏定时器
  controlBarHideTimer_->stop();

  // 确保控制栏可见
  if (controlBar_) {
    controlBar_->show();
  }

  // 显示菜单栏和状态栏
  menuBar()->show();
  statusBar()->show();

  // 退出全屏
  showNormal();

  // 恢复窗口状态
  if (wasMaximized_) {
    showMaximized();
  } else if (!normalGeometry_.isEmpty()) {
    setGeometry(normalGeometry_);
  }

  // 更新全屏按钮图标
  updateFullscreenButton();
}

void MainWindow::showControlBar() {
  if (!isFullscreen_) {
    return;
  }

  if (!isControlBarVisible_ && controlBar_) {
    controlBar_->show();
    isControlBarVisible_ = true;
  }

  // 重置自动隐藏定时器
  controlBarHideTimer_->start(3000);

  // 显示鼠标光标
  setCursor(Qt::ArrowCursor);
}

void MainWindow::hideControlBar() {
  if (!isFullscreen_) {
    return;
  }

  if (isControlBarVisible_ && controlBar_) {
    controlBar_->hide();
    isControlBarVisible_ = false;
  }

  // 隐藏鼠标光标
  setCursor(Qt::BlankCursor);
}

void MainWindow::updateFullscreenButton() {
  if (!fullscreenBtn_) {
    return;
  }

  if (isFullscreen_) {
    fullscreenBtn_->setText("⛉");  // 退出全屏图标
    fullscreenBtn_->setToolTip(tr("退出全屏 (ESC / F11)"));
  } else {
    fullscreenBtn_->setText("⛶");  // 全屏图标
    fullscreenBtn_->setToolTip(tr("全屏 (F11 / 双击视频)"));
  }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Escape:
      // ESC 键退出全屏
      if (isFullscreen_) {
        exitFullscreen();
        event->accept();
        return;
      }
      break;

    case Qt::Key_F11:
      // F11 切换全屏
      toggleFullscreen();
      event->accept();
      return;

    case Qt::Key_Space:
      // 空格键播放/暂停
      if (playPauseBtn_ && playPauseBtn_->isEnabled()) {
        playPauseBtn_->click();
        event->accept();
        return;
      }
      break;

    case Qt::Key_Left:
      // 左箭头后退5秒
      if (player_ && player_->IsOpened()) {
        int64_t current = player_->GetCurrentPlayTime();
        int64_t target = std::max(0LL, current - 5000);  // 减5秒（毫秒）
        player_->SeekAsync(target);
        event->accept();
        return;
      }
      break;

    case Qt::Key_Right:
      // 右箭头前进5秒
      if (player_ && player_->IsOpened()) {
        int64_t current = player_->GetCurrentPlayTime();
        int64_t duration = player_->GetDuration();
        int64_t target = std::min(duration, current + 5000);  // 加5秒（毫秒）
        player_->SeekAsync(target);
        event->accept();
        return;
      }
      break;

    default:
      break;
  }

  QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  // 在全屏模式下，监听鼠标移动以显示/隐藏控制栏
  if (isFullscreen_ && event->type() == QEvent::MouseMove) {
    // 鼠标移动时显示控制栏
    showControlBar();
  }

  return QMainWindow::eventFilter(obj, event);
}

}  // namespace zenplay
