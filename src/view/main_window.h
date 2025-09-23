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

 private:
  void setupUI();
  void setupMenuBar();
  void setupVideoArea();
  void setupControlBar();
  void setupStatusBar();
  void updateControlBarState();
  void setMediaFile(const QString& filePath);
  void updateProgressDisplay(int currentTime, int totalTime);
  void resetProgress();
  QString formatTime(int seconds) const;

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

  // State
  bool isDraggingProgress_;
  bool isFullscreen_;
  QString currentMediaPath_;
  int totalDuration_;

  // Window properties
  QSize normalSize_;
  QPoint normalPosition_;
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
