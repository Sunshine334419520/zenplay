// SDL渲染器使用示例
// 展示如何在Windows环境下集成SDL渲染器

#include <QApplication>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "player/video_player.h"

#ifdef _WIN32
#include <windows.h>

#include <QWinTaskbarButton>
#include <QWindow>
#endif

class VideoPlayerWidget : public QWidget {
  Q_OBJECT

 public:
  VideoPlayerWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setupUI();
    player_ = std::make_unique<zenplay::VideoPlayer>();
  }

  ~VideoPlayerWidget() = default;

 private slots:
  void openFile() {
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("Open Video File"), "",
        tr("Video Files (*.mp4 *.avi *.mkv *.mov *.wmv)"));

    if (!fileName.isEmpty()) {
      if (player_->Open(fileName.toStdString())) {
        statusLabel_->setText("File opened successfully");

        // 设置渲染窗口 - 使用QWidget的window handle
#ifdef _WIN32
        HWND hwnd = reinterpret_cast<HWND>(videoWidget_->winId());
        if (player_->SetRenderWindow(hwnd, videoWidget_->width(),
                                     videoWidget_->height())) {
          statusLabel_->setText("Renderer initialized successfully");
        } else {
          statusLabel_->setText("Failed to initialize renderer");
        }
#endif
      } else {
        statusLabel_->setText("Failed to open file");
      }
    }
  }

  void playVideo() {
    if (player_->Play()) {
      statusLabel_->setText("Playing...");
    } else {
      statusLabel_->setText("Failed to play");
    }
  }

  void pauseVideo() {
    if (player_->Pause()) {
      statusLabel_->setText("Paused");
    }
  }

  void stopVideo() {
    if (player_->Stop()) {
      statusLabel_->setText("Stopped");
    }
  }

 private:
  void setupUI() {
    setWindowTitle("ZenPlay SDL Renderer Demo");
    resize(800, 600);

    auto* layout = new QVBoxLayout(this);

    // 视频显示区域
    videoWidget_ = new QWidget(this);
    videoWidget_->setMinimumSize(640, 480);
    videoWidget_->setStyleSheet("background-color: black;");
    layout->addWidget(videoWidget_);

    // 控制按钮
    auto* buttonLayout = new QHBoxLayout();

    openButton_ = new QPushButton("Open File", this);
    playButton_ = new QPushButton("Play", this);
    pauseButton_ = new QPushButton("Pause", this);
    stopButton_ = new QPushButton("Stop", this);

    buttonLayout->addWidget(openButton_);
    buttonLayout->addWidget(playButton_);
    buttonLayout->addWidget(pauseButton_);
    buttonLayout->addWidget(stopButton_);

    layout->addLayout(buttonLayout);

    // 状态标签
    statusLabel_ = new QLabel("Ready", this);
    layout->addWidget(statusLabel_);

    // 连接信号槽
    connect(openButton_, &QPushButton::clicked, this,
            &VideoPlayerWidget::openFile);
    connect(playButton_, &QPushButton::clicked, this,
            &VideoPlayerWidget::playVideo);
    connect(pauseButton_, &QPushButton::clicked, this,
            &VideoPlayerWidget::pauseVideo);
    connect(stopButton_, &QPushButton::clicked, this,
            &VideoPlayerWidget::stopVideo);
  }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);

    // 当窗口大小改变时，通知渲染器
    if (player_) {
      // TODO: 添加渲染器resize通知
      // player_->OnResize(videoWidget_->width(), videoWidget_->height());
    }
  }

 private:
  std::unique_ptr<zenplay::VideoPlayer> player_;

  QWidget* videoWidget_;
  QPushButton* openButton_;
  QPushButton* playButton_;
  QPushButton* pauseButton_;
  QPushButton* stopButton_;
  QLabel* statusLabel_;
};

// 如果要在main.cpp中使用
/*
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    VideoPlayerWidget window;
    window.show();

    return app.exec();
}
*/

#include "sdl_renderer_demo.moc"
