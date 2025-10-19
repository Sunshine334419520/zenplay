#pragma once

#include <QAction>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QStatusBar>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>

#include "player/common/player_state_manager.h"

namespace zenplay {

class ZenPlayer;

class VideoDisplayWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void openLocalFile();
  void openNetworkUrl();
  void togglePlayPause();
  void stopPlayback();
  void onProgressSliderPressed();
  void onProgressSliderReleased();
  void onProgressSliderValueChanged(int value);
  void onVolumeSliderValueChanged(int value);
  void updatePlaybackProgress();
  void onPlayerStateChanged();

  /**
   * @brief 处理播放器状态变化（异步 Seek 回调）
   * @param old_state 旧状态
   * @param new_state 新状态
   */
  void handlePlayerStateChanged(PlayerStateManager::PlayerState old_state,
                                PlayerStateManager::PlayerState new_state);

 private:
  void setupUI();
  void setupMenuBar();
  void setupVideoArea();
  void setupControlBar();
  void setupStatusBar();
  void updateControlBarState();
  void setMediaFile(const QString& filePath);
  void updateProgressDisplay(int64_t currentTimeMs, int64_t totalTimeMs);
  void resetProgress();
  QString formatTime(int64_t milliseconds) const;  // 格式化毫秒为 HH:MM:SS.mmm

  // 全屏相关
  void toggleFullscreen();
  void enterFullscreen();
  void exitFullscreen();
  void updateFullscreenButton();
  void keyPressEvent(QKeyEvent* event) override;

  // 控制栏自动隐藏（全屏时）
  void startControlBarHideTimer();
  void showControlBar();
  void hideControlBar();
  bool eventFilter(QObject* obj, QEvent* event) override;

 private:
  // UI Components
  QWidget* centralWidget_;
  QVBoxLayout* mainLayout_;

  // Video display area
  VideoDisplayWidget* videoWidget_;
  QFrame* videoFrame_;

  // Control bar
  QFrame* controlBar_;
  QHBoxLayout* controlLayout_;
  QPushButton* playPauseBtn_;
  QPushButton* stopBtn_;
  QLabel* timeLabel_;
  QSlider* progressSlider_;
  QLabel* durationLabel_;
  QLabel* volumeIcon_;
  QSlider* volumeSlider_;
  QPushButton* fullscreenBtn_;

  // Menu actions
  QAction* openFileAction_;
  QAction* openUrlAction_;
  QAction* exitAction_;
  QAction* aboutAction_;

  // Status bar
  QLabel* statusLabel_;

  // Player and timer
  std::unique_ptr<ZenPlayer> player_;
  QTimer* updateTimer_;
  QTimer* controlBarHideTimer_;  // 全屏时自动隐藏控制栏的定时器

  // State
  bool isDraggingProgress_;
  bool isFullscreen_;
  bool isControlBarVisible_;  // 控制栏是否可见（全屏时使用）
  QString currentMediaPath_;
  int64_t totalDuration_;  // 总时长（毫秒）
  int state_callback_id_;  // 状态回调 ID

  // Window properties
  QSize normalSize_;
  QPoint normalPosition_;
  QRect normalGeometry_;  // 保存窗口化模式的完整几何信息
  bool wasMaximized_;     // 进入全屏前是否是最大化状态
};

// Custom video display widget for SDL rendering
class VideoDisplayWidget : public QWidget {
  Q_OBJECT

 public:
  explicit VideoDisplayWidget(QWidget* parent = nullptr);
  ~VideoDisplayWidget() override = default;

  // Get native window handle for SDL
  void* getNativeHandle();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 signals:
  void doubleClicked();
  void resized(int width, int height);

 private:
  QLabel* placeholderLabel_;
};

}  // namespace zenplay
