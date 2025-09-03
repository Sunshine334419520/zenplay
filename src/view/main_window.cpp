#include "main_window.h"

#include <QApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMessageBox>
#include <QScreen>
#include <QUrl>
#include <QWindow>

#ifdef _WIN32
#include <windows.h>
// #include <QWinTaskbarButton>  // Removed because it's not used and may not be
// available
#endif

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
      isDraggingProgress_(false),
      isFullscreen_(false),
      totalDuration_(0) {
  setupUI();

  // Connect timer for progress updates
  connect(updateTimer_, &QTimer::timeout, this,
          &MainWindow::updatePlaybackProgress);
  updateTimer_->setInterval(100);  // Update every 100ms

  // Set window properties
  setMinimumSize(800, 600);
  resize(1200, 800);
  setWindowTitle("ZenPlay Media Player");
  setWindowIcon(QIcon(":/icons/zenplay.png"));  // You'll need to add this icon
}

MainWindow::~MainWindow() = default;

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
  connect(videoWidget_, &VideoDisplayWidget::doubleClicked, [this]() {
    // Toggle fullscreen on double click
    if (isFullscreen_) {
      showNormal();
      isFullscreen_ = false;
    } else {
      normalSize_ = size();
      normalPosition_ = pos();
      showFullScreen();
      isFullscreen_ = true;
    }
  });

  connect(videoWidget_, &VideoDisplayWidget::resized,
          [this](int width, int height) {
            // Notify player about size change if needed
            if (player_) {
              // TODO: Handle video widget resize
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
  connect(fullscreenBtn_, &QPushButton::clicked, [this]() {
    videoWidget_->doubleClicked();  // Reuse the double-click logic
  });
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

  // Stop current playback
  stopPlayback();

  // Try to open the file
  if (player_->Open(filePath.toStdString())) {
    currentMediaPath_ = filePath;

    // Setup SDL renderer with video widget handle
    void* handle = videoWidget_->getNativeHandle();
    if (handle && player_->SetRenderWindow(handle, videoWidget_->width(),
                                           videoWidget_->height())) {
      statusLabel_->setText(tr("Media loaded successfully"));

      // Get duration and update UI
      totalDuration_ = player_->GetDuration();
      durationLabel_->setText(formatTime(totalDuration_));
      progressSlider_->setMaximum(totalDuration_);

      updateControlBarState();

      // Show filename in window title
      QFileInfo fileInfo(filePath);
      setWindowTitle(tr("ZenPlay - %1").arg(fileInfo.fileName()));
    } else {
      statusLabel_->setText(tr("Failed to initialize renderer"));
      QMessageBox::critical(this, tr("Error"),
                            tr("Failed to initialize video renderer."));
    }
  } else {
    statusLabel_->setText(tr("Failed to open media file"));
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to open media file:\n%1").arg(filePath));
  }
}

void MainWindow::togglePlayPause() {
  if (!player_) {
    return;
  }

  switch (player_->GetState()) {
    case ZenPlayer::PlayState::kStopped:
      if (player_->Play()) {
        updateTimer_->start();
        statusLabel_->setText(tr("Playing"));
      }
      break;
    case ZenPlayer::PlayState::kPlaying:
      if (player_->Pause()) {
        updateTimer_->stop();
        statusLabel_->setText(tr("Paused"));
      }
      break;
    case ZenPlayer::PlayState::kPaused:
      if (player_->Play()) {
        updateTimer_->start();
        statusLabel_->setText(tr("Playing"));
      }
      break;
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
    timeLabel_->setText("00:00");

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

  int seekTime = progressSlider_->value();
  if (player_->Seek(seekTime * 1000)) {  // Convert to milliseconds
    statusLabel_->setText(tr("Seeking..."));
  }

  isDraggingProgress_ = false;
}

void MainWindow::onProgressSliderValueChanged(int value) {
  if (!isDraggingProgress_) {
    return;
  }

  // Update time label while dragging
  timeLabel_->setText(formatTime(value));
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

  // TODO: Get actual playback position from player
  // For now, just simulate progress
  static int currentTime = 0;

  if (player_->GetState() == ZenPlayer::PlayState::kPlaying) {
    currentTime++;
    if (currentTime <= totalDuration_) {
      updateProgressDisplay(currentTime, totalDuration_);
    }
  }
}

void MainWindow::updateProgressDisplay(int currentTime, int totalTime) {
  if (!isDraggingProgress_) {
    progressSlider_->setValue(currentTime);
    timeLabel_->setText(formatTime(currentTime));
  }
}

void MainWindow::updateControlBarState() {
  if (!player_) {
    return;
  }

  bool hasMedia = player_->IsOpened();
  bool isPlaying = (player_->GetState() == ZenPlayer::PlayState::kPlaying);

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

QString MainWindow::formatTime(int seconds) const {
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;

  if (hours > 0) {
    return QString("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
  } else {
    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
  }
}

void MainWindow::onPlayerStateChanged() {
  updateControlBarState();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);

  // Update SDL renderer size when window resizes
  if (player_ && videoWidget_) {
    // TODO: Notify renderer about size change
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
  Q_UNUSED(event)
  emit doubleClicked();
}

}  // namespace zenplay
