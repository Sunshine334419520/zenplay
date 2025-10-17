# ZenPlay 功能完善建议

> 文档版本: 1.0  
> 创建日期: 2024-12-19  
> 最后更新: 2024-12-19

本文档列出了 ZenPlay 项目可以完善和扩展的功能特性，按照用户体验价值和实现难度分类，为后续功能开发提供方向。

---

## 📋 目录

- [优先级说明](#优先级说明)
- [播放增强](#播放增强)
  - [P0 - 基础必备功能](#p0---基础必备功能)
  - [P1 - 重要体验功能](#p1---重要体验功能)
  - [P2 - 高级播放功能](#p2---高级播放功能)
- [用户界面](#用户界面)
  - [P0 - UI 基础完善](#p0---ui-基础完善)
  - [P1 - UI 体验优化](#p1---ui-体验优化)
  - [P2 - UI 高级功能](#p2---ui-高级功能)
- [媒体处理](#媒体处理)
  - [P1 - 字幕支持](#p1---字幕支持)
  - [P1 - 音频增强](#p1---音频增强)
  - [P2 - 视频效果](#p2---视频效果)
- [工具功能](#工具功能)
  - [P1 - 截图和录制](#p1---截图和录制)
  - [P2 - 高级工具](#p2---高级工具)
- [系统功能](#系统功能)
- [实施路线图](#实施路线图)

---

## 优先级说明

| 优先级 | 说明 | 用户价值 | 建议时间 |
|--------|------|---------|---------|
| **P0** | 基础必备功能，严重影响用户体验 | 极高 | 立即开发 |
| **P1** | 重要功能，明显提升用户体验 | 高 | 近期完成 |
| **P2** | 高级功能，锦上添花 | 中等 | 长期规划 |

---

## 播放增强

### P0 - 基础必备功能

#### 1. 🎵 播放列表管理

**功能描述**:
- 支持创建、保存、加载播放列表
- 支持拖拽排序、添加/删除文件
- 显示当前播放项，高亮标记
- 支持循环模式（单曲循环、列表循环、随机播放）

**用户场景**:
- 连续播放多个视频文件
- 创建临时播放列表观看剧集
- 保存常用播放列表（如教学视频、音乐合集）

**实现方案**:

```cpp
// playlist_manager.h
class PlaylistManager {
 public:
  struct PlaylistItem {
    std::string file_path;
    std::string title;
    int64_t duration_ms;
    QPixmap thumbnail;  // 可选：缩略图
  };

  enum class PlayMode {
    SEQUENTIAL,  // 顺序播放
    LOOP_ONE,    // 单曲循环
    LOOP_ALL,    // 列表循环
    SHUFFLE      // 随机播放
  };

  // 播放列表操作
  void AddItem(const PlaylistItem& item);
  void RemoveItem(size_t index);
  void MoveItem(size_t from, size_t to);
  void Clear();

  // 播放控制
  bool Next();
  bool Previous();
  void SetPlayMode(PlayMode mode);
  void SetCurrentIndex(size_t index);

  // 持久化
  bool SaveToFile(const std::string& filepath);
  bool LoadFromFile(const std::string& filepath);

  // 信号通知
  using OnPlaylistChangedCallback = std::function<void()>;
  using OnCurrentItemChangedCallback = std::function<void(size_t)>;

  void RegisterPlaylistChangedCallback(OnPlaylistChangedCallback cb);
  void RegisterCurrentItemChangedCallback(OnCurrentItemChangedCallback cb);

 private:
  std::vector<PlaylistItem> items_;
  size_t current_index_ = 0;
  PlayMode play_mode_ = PlayMode::SEQUENTIAL;
  std::vector<size_t> shuffle_order_;  // 随机播放顺序
};
```

**UI 设计**:
```cpp
// playlist_widget.h
class PlaylistWidget : public QWidget {
  Q_OBJECT

 public:
  PlaylistWidget(PlaylistManager* manager, QWidget* parent = nullptr);

 private slots:
  void onItemDoubleClicked(const QModelIndex& index);
  void onAddFiles();
  void onRemoveSelected();
  void onClearAll();
  void onSavePlaylist();
  void onLoadPlaylist();
  void onPlayModeChanged();

 private:
  void setupUI();
  void updatePlaylist();

  PlaylistManager* playlist_manager_;
  QListWidget* list_widget_;
  QPushButton* add_btn_;
  QPushButton* remove_btn_;
  QPushButton* clear_btn_;
  QComboBox* play_mode_combo_;
};
```

**播放列表格式 (JSON)**:
```json
{
  "name": "My Playlist",
  "version": "1.0",
  "play_mode": "sequential",
  "items": [
    {
      "path": "/path/to/video1.mp4",
      "title": "Episode 1",
      "duration_ms": 1234567
    },
    {
      "path": "/path/to/video2.mp4",
      "title": "Episode 2",
      "duration_ms": 2345678
    }
  ]
}
```

**实施难度**: ⭐⭐ (简单-中等)  
**预期开发时间**: 1 周  
**用户价值**: ⭐⭐⭐⭐⭐ (极高)

---

#### 2. ⏩ 倍速播放

**功能描述**:
- 支持 0.25x - 4.0x 倍速播放
- 常用倍速预设（0.5x, 0.75x, 1.0x, 1.25x, 1.5x, 2.0x）
- 保持音调不变（使用 SoundTouch 或 FFmpeg 的 atempo 滤镜）

**用户场景**:
- 快速预览视频内容
- 慢速播放学习复杂内容
- 加速观看教学视频

**实现方案**:

```cpp
// playback_controller.h
class PlaybackController {
 public:
  void SetPlaybackSpeed(float speed);  // 0.25 - 4.0
  float GetPlaybackSpeed() const;

 private:
  void ApplySpeedToAudio(float speed);
  void ApplySpeedToVideo(float speed);

  float playback_speed_ = 1.0f;
};
```

**音频变速实现 (使用 FFmpeg atempo 滤镜)**:
```cpp
void AudioPlayer::SetPlaybackSpeed(float speed) {
  if (speed < 0.5f || speed > 2.0f) {
    // atempo 滤镜限制 0.5-2.0，需要级联
    // 例如 4.0x = 2.0x * 2.0x
    MODULE_WARN(LOG_MODULE_AUDIO, 
                "Speed {} requires filter chain", speed);
    // 实现级联滤镜...
  }

  // 创建 atempo 滤镜
  char filter_desc[256];
  snprintf(filter_desc, sizeof(filter_desc), "atempo=%.2f", speed);

  AVFilterGraph* filter_graph = avfilter_graph_alloc();
  AVFilterContext* buffersrc_ctx;
  AVFilterContext* buffersink_ctx;

  // 配置 buffer source
  const AVFilter* buffersrc = avfilter_get_by_name("abuffer");
  char args[512];
  snprintf(args, sizeof(args),
           "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
           audio_spec_.sample_rate, audio_spec_.sample_rate,
           av_get_sample_fmt_name(AV_SAMPLE_FMT_FLT),
           av_get_default_channel_layout(audio_spec_.channels));

  avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                args, nullptr, filter_graph);

  // 配置 buffer sink
  const AVFilter* buffersink = avfilter_get_by_name("abuffersink");
  avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                nullptr, nullptr, filter_graph);

  // 创建 atempo 滤镜
  AVFilterContext* atempo_ctx;
  const AVFilter* atempo_filter = avfilter_get_by_name("atempo");
  avfilter_graph_create_filter(&atempo_ctx, atempo_filter, "atempo",
                                filter_desc, nullptr, filter_graph);

  // 连接滤镜
  avfilter_link(buffersrc_ctx, 0, atempo_ctx, 0);
  avfilter_link(atempo_ctx, 0, buffersink_ctx, 0);

  // 配置图
  avfilter_graph_config(filter_graph, nullptr);

  // 保存滤镜图
  audio_filter_graph_ = filter_graph;
  audio_buffersrc_ctx_ = buffersrc_ctx;
  audio_buffersink_ctx_ = buffersink_ctx;
}

void AudioPlayer::ProcessFrameWithFilter(AVFrame* input_frame, 
                                         AVFrame** output_frame) {
  // 推送帧到滤镜
  av_buffersrc_add_frame_flags(audio_buffersrc_ctx_, input_frame, 
                               AV_BUFFERSRC_FLAG_KEEP_REF);

  // 从滤镜获取处理后的帧
  AVFrame* filtered_frame = av_frame_alloc();
  int ret = av_buffersink_get_frame(audio_buffersink_ctx_, filtered_frame);
  
  if (ret >= 0) {
    *output_frame = filtered_frame;
  }
}
```

**视频变速实现 (调整帧间隔)**:
```cpp
void VideoPlayer::SetPlaybackSpeed(float speed) {
  playback_speed_ = speed;
  
  // 调整帧显示间隔
  // 原始帧率 30fps -> 33.33ms/帧
  // 2.0x 倍速 -> 16.67ms/帧
  // 0.5x 倍速 -> 66.67ms/帧
}

void VideoPlayer::RenderLoop() {
  while (running_) {
    auto delay = CalculateVideoDelay(...);
    
    // 应用倍速调整
    delay /= playback_speed_;  // ✅ 倍速播放
    
    if (delay > 0) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(static_cast<int>(delay)));
    }
    
    RenderFrame();
  }
}
```

**UI 控件**:
```cpp
// main_window.cpp
void MainWindow::setupControlBar() {
  // 倍速选择下拉框
  speed_combo_ = new QComboBox(this);
  speed_combo_->addItem("0.25x", 0.25f);
  speed_combo_->addItem("0.5x", 0.5f);
  speed_combo_->addItem("0.75x", 0.75f);
  speed_combo_->addItem("1.0x (Normal)", 1.0f);
  speed_combo_->addItem("1.25x", 1.25f);
  speed_combo_->addItem("1.5x", 1.5f);
  speed_combo_->addItem("2.0x", 2.0f);
  speed_combo_->addItem("4.0x", 4.0f);
  speed_combo_->setCurrentIndex(3);  // 默认 1.0x

  connect(speed_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MainWindow::onSpeedChanged);
}

void MainWindow::onSpeedChanged(int index) {
  float speed = speed_combo_->itemData(index).toFloat();
  player_->SetPlaybackSpeed(speed);
}
```

**实施难度**: ⭐⭐⭐ (中等)  
**预期开发时间**: 1 周  
**用户价值**: ⭐⭐⭐⭐⭐ (极高)

---

### P1 - 重要体验功能

#### 3. ⏭️ 快进/快退步进控制

**功能描述**:
- 支持固定步长跳转（5秒、10秒、30秒、1分钟）
- 键盘快捷键（左箭头后退 5秒，右箭头前进 5秒）
- 鼠标滚轮微调（滚动前进/后退 1秒）

**实现方案**:

```cpp
// main_window.cpp
void MainWindow::setupShortcuts() {
  // 快进/快退快捷键
  auto* skip_forward_5s = new QShortcut(Qt::Key_Right, this);
  connect(skip_forward_5s, &QShortcut::activated, 
          this, [this]() { skipTime(5000); });

  auto* skip_backward_5s = new QShortcut(Qt::Key_Left, this);
  connect(skip_backward_5s, &QShortcut::activated, 
          this, [this]() { skipTime(-5000); });

  auto* skip_forward_30s = new QShortcut(Qt::CTRL + Qt::Key_Right, this);
  connect(skip_forward_30s, &QShortcut::activated, 
          this, [this]() { skipTime(30000); });

  auto* skip_backward_30s = new QShortcut(Qt::CTRL + Qt::Key_Left, this);
  connect(skip_backward_30s, &QShortcut::activated, 
          this, [this]() { skipTime(-30000); });

  // 鼠标滚轮控制
  video_display_->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  if (obj == video_display_ && event->type() == QEvent::Wheel) {
    QWheelEvent* wheel_event = static_cast<QWheelEvent*>(event);
    int delta = wheel_event->angleDelta().y();
    
    if (delta > 0) {
      skipTime(1000);   // 前进 1 秒
    } else {
      skipTime(-1000);  // 后退 1 秒
    }
    
    return true;  // 事件已处理
  }
  
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::skipTime(int64_t offset_ms) {
  int64_t current_time = player_->GetCurrentPlayTime();
  int64_t duration = player_->GetDuration();
  int64_t target_time = std::clamp(current_time + offset_ms, 
                                   0LL, duration);
  
  player_->SeekAsync(target_time);
  
  // 显示临时提示 (OSD)
  showOSD(QString("%1%2s")
          .arg(offset_ms > 0 ? "+" : "")
          .arg(offset_ms / 1000.0, 0, 'f', 1));
}

void MainWindow::showOSD(const QString& text) {
  // 创建半透明提示框
  if (!osd_label_) {
    osd_label_ = new QLabel(video_display_);
    osd_label_->setStyleSheet(
        "background-color: rgba(0, 0, 0, 180);"
        "color: white;"
        "font-size: 24px;"
        "padding: 10px 20px;"
        "border-radius: 5px;");
    osd_label_->setAlignment(Qt::AlignCenter);
  }

  osd_label_->setText(text);
  osd_label_->adjustSize();
  
  // 居中显示
  int x = (video_display_->width() - osd_label_->width()) / 2;
  int y = (video_display_->height() - osd_label_->height()) / 2;
  osd_label_->move(x, y);
  osd_label_->show();

  // 2 秒后自动隐藏
  QTimer::singleShot(2000, osd_label_, &QLabel::hide);
}
```

**实施难度**: ⭐ (简单)  
**预期开发时间**: 2 天  
**用户价值**: ⭐⭐⭐⭐ (高)

---

#### 4. 📌 书签/标记功能

**功能描述**:
- 在视频中添加书签标记
- 快速跳转到书签位置
- 保存书签到文件，下次打开自动加载

**用户场景**:
- 标记视频中的精彩片段
- 学习视频时标记重点位置
- 长视频中快速定位

**实现方案**:

```cpp
// bookmark_manager.h
class BookmarkManager {
 public:
  struct Bookmark {
    int64_t timestamp_ms;
    std::string title;
    std::string description;
    QPixmap thumbnail;  // 可选
  };

  void AddBookmark(const Bookmark& bookmark);
  void RemoveBookmark(size_t index);
  const std::vector<Bookmark>& GetBookmarks() const;

  bool SaveToFile(const std::string& video_path);
  bool LoadFromFile(const std::string& video_path);

 private:
  std::vector<Bookmark> bookmarks_;
};
```

**UI 控件**:
```cpp
// bookmark_widget.h
class BookmarkWidget : public QWidget {
  Q_OBJECT

 public:
  BookmarkWidget(BookmarkManager* manager, QWidget* parent = nullptr);

 signals:
  void bookmarkSelected(int64_t timestamp_ms);

 private slots:
  void onAddBookmark();
  void onRemoveBookmark();
  void onBookmarkClicked(const QModelIndex& index);

 private:
  BookmarkManager* bookmark_manager_;
  QListWidget* bookmark_list_;
};
```

**存储格式 (JSON)**:
```json
{
  "video_path": "/path/to/video.mp4",
  "bookmarks": [
    {
      "timestamp_ms": 12345,
      "title": "精彩片段",
      "description": "主角登场"
    },
    {
      "timestamp_ms": 67890,
      "title": "重点内容",
      "description": "算法讲解"
    }
  ]
}
```

**实施难度**: ⭐⭐ (简单)  
**预期开发时间**: 3 天  
**用户价值**: ⭐⭐⭐ (中等)

---

### P2 - 高级播放功能

#### 5. 🔊 音频轨道切换

**功能描述**:
- 检测视频中的多个音频轨道
- 在播放过程中动态切换音轨
- 显示音轨信息（语言、编码、声道）

**用户场景**:
- 多语言电影切换配音
- 选择评论音轨

**实现方案**:

```cpp
// demuxer.h
class Demuxer {
 public:
  struct AudioTrackInfo {
    int stream_index;
    std::string language;
    std::string codec_name;
    int channels;
    int sample_rate;
  };

  std::vector<AudioTrackInfo> GetAudioTracks() const;
  bool SwitchAudioTrack(int stream_index);

 private:
  int current_audio_stream_index_ = -1;
};

// playback_controller.h
class PlaybackController {
 public:
  std::vector<Demuxer::AudioTrackInfo> GetAudioTracks() const;
  bool SwitchAudioTrack(int stream_index);

 private:
  void OnAudioTrackChanged(int new_stream_index);
};
```

**实施难度**: ⭐⭐⭐ (中等)  
**预期开发时间**: 1 周  
**用户价值**: ⭐⭐⭐ (中等)

---

## 用户界面

### P0 - UI 基础完善

#### 6. 🎨 进度条预览缩略图

**功能描述**:
- 鼠标悬停在进度条上显示预览缩略图
- 显示对应时间点的画面
- 显示时间戳

**用户场景**:
- 快速定位视频中的特定场景
- 预览跳转目标位置

**实现方案**:

```cpp
// progress_bar_widget.h
class ProgressBarWidget : public QSlider {
  Q_OBJECT

 public:
  ProgressBarWidget(Qt::Orientation orientation, QWidget* parent = nullptr);

  void SetThumbnailProvider(ThumbnailProvider* provider);

 protected:
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  bool event(QEvent* event) override;

 private:
  void showThumbnail(int position_ms);
  void hideThumbnail();

  ThumbnailProvider* thumbnail_provider_ = nullptr;
  QLabel* thumbnail_label_ = nullptr;
  QLabel* time_label_ = nullptr;
};

// thumbnail_provider.h
class ThumbnailProvider {
 public:
  ThumbnailProvider(VideoDecoder* decoder);

  QPixmap GetThumbnail(int64_t timestamp_ms);

 private:
  void GenerateThumbnails();  // 预生成缩略图
  QPixmap SeekAndCapture(int64_t timestamp_ms);

  std::map<int64_t, QPixmap> thumbnail_cache_;
  VideoDecoder* decoder_;
};
```

**实施难度**: ⭐⭐⭐ (中等)  
**预期开发时间**: 1 周  
**用户价值**: ⭐⭐⭐⭐ (高)

---

#### 7. 📺 全屏模式优化

**功能描述**:
- 真正的无边框全屏
- 全屏时隐藏菜单栏、状态栏
- 鼠标移动显示控制栏，静止 3 秒后自动隐藏
- 双击视频区域切换全屏
- ESC 键退出全屏

**实现方案**:

```cpp
// main_window.cpp
void MainWindow::toggleFullScreen() {
  if (isFullScreen()) {
    // 退出全屏
    showNormal();
    menuBar()->show();
    statusBar()->show();
    hideControlsTimer_->stop();
  } else {
    // 进入全屏
    showFullScreen();
    menuBar()->hide();
    statusBar()->hide();
    
    // 启动自动隐藏定时器
    hideControlsTimer_->start(3000);  // 3 秒后隐藏
  }
}

void MainWindow::setupMouseTracking() {
  setMouseTracking(true);
  video_display_->setMouseTracking(true);

  hideControlsTimer_ = new QTimer(this);
  hideControlsTimer_->setSingleShot(true);
  connect(hideControlsTimer_, &QTimer::timeout, 
          this, &MainWindow::hideControlsInFullScreen);
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
  if (isFullScreen()) {
    // 显示控制栏
    control_bar_->show();
    setCursor(Qt::ArrowCursor);
    
    // 重置自动隐藏定时器
    hideControlsTimer_->start(3000);
  }

  QMainWindow::mouseMoveEvent(event);
}

void MainWindow::hideControlsInFullScreen() {
  if (isFullScreen()) {
    control_bar_->hide();
    setCursor(Qt::BlankCursor);  // 隐藏鼠标光标
  }
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    // 双击切换全屏
    if (childAt(event->pos()) == video_display_) {
      toggleFullScreen();
    }
  }
}
```

**实施难度**: ⭐⭐ (简单)  
**预期开发时间**: 2 天  
**用户价值**: ⭐⭐⭐⭐⭐ (极高)

---

### P1 - UI 体验优化

#### 8. 🎛️ 右键菜单功能

**功能描述**:
- 视频区域右键显示上下文菜单
- 常用操作：播放/暂停、全屏、音量、倍速、书签等
- 快捷键显示

**实现方案**:

```cpp
// main_window.cpp
void MainWindow::setupContextMenu() {
  video_display_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(video_display_, &QWidget::customContextMenuRequested,
          this, &MainWindow::showContextMenu);
}

void MainWindow::showContextMenu(const QPoint& pos) {
  QMenu context_menu(this);

  // 播放控制
  QAction* play_action = context_menu.addAction("播放/暂停");
  play_action->setShortcut(QKeySequence(Qt::Key_Space));
  connect(play_action, &QAction::triggered, 
          this, &MainWindow::togglePlayPause);

  QAction* stop_action = context_menu.addAction("停止");
  connect(stop_action, &QAction::triggered, 
          this, &MainWindow::stopPlayback);

  context_menu.addSeparator();

  // 倍速菜单
  QMenu* speed_menu = context_menu.addMenu("播放速度");
  for (float speed : {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f}) {
    QAction* speed_action = speed_menu->addAction(
        QString("%1x").arg(speed));
    connect(speed_action, &QAction::triggered, 
            this, [this, speed]() { player_->SetPlaybackSpeed(speed); });
  }

  // 音量菜单
  QMenu* volume_menu = context_menu.addMenu("音量");
  QSlider* volume_slider = new QSlider(Qt::Horizontal);
  volume_slider->setRange(0, 100);
  volume_slider->setValue(static_cast<int>(player_->GetVolume() * 100));
  connect(volume_slider, &QSlider::valueChanged, 
          this, [this](int value) { 
            player_->SetVolume(value / 100.0f); 
          });
  QWidgetAction* volume_action = new QWidgetAction(&context_menu);
  volume_action->setDefaultWidget(volume_slider);
  volume_menu->addAction(volume_action);

  context_menu.addSeparator();

  // 全屏
  QAction* fullscreen_action = context_menu.addAction("全屏");
  fullscreen_action->setShortcut(QKeySequence(Qt::Key_F11));
  connect(fullscreen_action, &QAction::triggered, 
          this, &MainWindow::toggleFullScreen);

  // 显示菜单
  context_menu.exec(video_display_->mapToGlobal(pos));
}
```

**实施难度**: ⭐ (简单)  
**预期开发时间**: 1 天  
**用户价值**: ⭐⭐⭐ (中等)

---

#### 9. 📊 实时信息叠加 (OSD)

**功能描述**:
- 显示播放状态（播放/暂停/缓冲）
- 显示当前时间、总时长
- 显示音量调整提示
- 显示倍速调整提示
- 显示文件信息（分辨率、码率、帧率）

**实现方案**:

```cpp
// osd_widget.h
class OSDWidget : public QWidget {
  Q_OBJECT

 public:
  enum class OSDType {
    PLAY_PAUSE,
    VOLUME,
    SPEED,
    SEEK,
    FILE_INFO,
    BUFFERING
  };

  OSDWidget(QWidget* parent = nullptr);

  void ShowMessage(OSDType type, const QString& message, 
                   int duration_ms = 2000);
  void ShowFileInfo(const QString& info);
  void ShowBuffering(int percent);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void updatePosition();

  QString current_message_;
  QTimer* hide_timer_;
};

// main_window.cpp
void MainWindow::setupOSD() {
  osd_widget_ = new OSDWidget(video_display_);
  osd_widget_->hide();
}

void MainWindow::onVolumeChanged(int volume) {
  player_->SetVolume(volume / 100.0f);
  osd_widget_->ShowMessage(
      OSDWidget::OSDType::VOLUME,
      QString("音量: %1%").arg(volume));
}

void MainWindow::onSpeedChanged(float speed) {
  player_->SetPlaybackSpeed(speed);
  osd_widget_->ShowMessage(
      OSDWidget::OSDType::SPEED,
      QString("速度: %1x").arg(speed));
}

void MainWindow::togglePlayPause() {
  if (player_->GetState() == PlayerState::kPlaying) {
    player_->Pause();
    osd_widget_->ShowMessage(OSDWidget::OSDType::PLAY_PAUSE, "暂停");
  } else {
    player_->Play();
    osd_widget_->ShowMessage(OSDWidget::OSDType::PLAY_PAUSE, "播放");
  }
}
```

**实施难度**: ⭐⭐ (简单)  
**预期开发时间**: 3 天  
**用户价值**: ⭐⭐⭐⭐ (高)

---

### P2 - UI 高级功能

#### 10. 🌈 多主题支持

**功能描述**:
- 内置多种主题（深色、浅色、跟随系统）
- 支持自定义主题配色
- 保存用户主题偏好

**实施难度**: ⭐⭐ (简单)  
**预期开发时间**: 3 天  
**用户价值**: ⭐⭐⭐ (中等)

---

## 媒体处理

### P1 - 字幕支持

#### 11. 📝 外挂字幕加载

**功能描述**:
- 支持 SRT、ASS、SSA 字幕格式
- 自动搜索同名字幕文件
- 支持拖拽加载字幕
- 字幕时间轴调整（提前/延后）

**实现方案**:

```cpp
// subtitle_parser.h
class SubtitleParser {
 public:
  struct SubtitleItem {
    int64_t start_time_ms;
    int64_t end_time_ms;
    std::string text;
    std::map<std::string, std::string> styles;  // ASS 样式
  };

  static std::vector<SubtitleItem> ParseSRT(const std::string& filepath);
  static std::vector<SubtitleItem> ParseASS(const std::string& filepath);
};

// subtitle_renderer.h
class SubtitleRenderer {
 public:
  SubtitleRenderer(QWidget* video_widget);

  void LoadSubtitle(const std::string& filepath);
  void SetTimeOffset(int64_t offset_ms);  // 字幕时间偏移
  void SetFontSize(int size);
  void SetFontColor(const QColor& color);

  void UpdateCurrentTime(int64_t current_time_ms);
  QString GetCurrentSubtitle() const;

 private:
  std::vector<SubtitleParser::SubtitleItem> subtitles_;
  int64_t time_offset_ms_ = 0;
  size_t current_index_ = 0;
};

// subtitle_widget.h
class SubtitleWidget : public QLabel {
  Q_OBJECT

 public:
  SubtitleWidget(QWidget* parent = nullptr);

  void ShowSubtitle(const QString& text);
  void Clear();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QString subtitle_text_;
};
```

**SRT 字幕解析示例**:
```cpp
std::vector<SubtitleItem> SubtitleParser::ParseSRT(
    const std::string& filepath) {
  std::vector<SubtitleItem> items;
  std::ifstream file(filepath);
  std::string line;

  while (std::getline(file, line)) {
    // 跳过序号行
    if (line.empty() || std::isdigit(line[0])) continue;

    // 解析时间戳: 00:00:10,500 --> 00:00:13,000
    if (line.find("-->") != std::string::npos) {
      SubtitleItem item;
      sscanf(line.c_str(), "%02d:%02d:%02d,%03d --> %02d:%02d:%02d,%03d",
             &h1, &m1, &s1, &ms1, &h2, &m2, &s2, &ms2);
      item.start_time_ms = (h1*3600 + m1*60 + s1)*1000 + ms1;
      item.end_time_ms = (h2*3600 + m2*60 + s2)*1000 + ms2;

      // 读取字幕文本（可能多行）
      std::string text;
      while (std::getline(file, line) && !line.empty()) {
        if (!text.empty()) text += "\n";
        text += line;
      }
      item.text = text;

      items.push_back(item);
    }
  }

  return items;
}
```

**实施难度**: ⭐⭐⭐ (中等)  
**预期开发时间**: 1 周  
**用户价值**: ⭐⭐⭐⭐⭐ (极高)

---

### P1 - 音频增强

#### 12. 🎚️ 均衡器 (EQ)

**功能描述**:
- 10频段图形均衡器
- 预设音效模式（摇滚、流行、古典、爵士等）
- 自定义 EQ 设置保存

**实现方案**:

```cpp
// audio_equalizer.h
class AudioEqualizer {
 public:
  static constexpr int kNumBands = 10;
  static constexpr int kFrequencies[kNumBands] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000  // Hz
  };

  struct EQPreset {
    std::string name;
    float gains[kNumBands];  // dB, range: -12 to +12
  };

  AudioEqualizer();

  void SetBandGain(int band_index, float gain_db);
  float GetBandGain(int band_index) const;

  void ApplyPreset(const EQPreset& preset);
  void ResetToFlat();

  void ProcessAudio(float* samples, int num_samples, int channels);

 private:
  void UpdateFilters();

  float band_gains_[kNumBands] = {0.0f};  // 默认 0dB (不调整)
  std::unique_ptr<BiquadFilter> filters_[kNumBands];
};

// 预设
const AudioEqualizer::EQPreset kRockPreset = {
  "Rock",
  {5.0f, 3.0f, -3.0f, -5.0f, -2.0f, 1.0f, 4.0f, 6.0f, 6.0f, 6.0f}
};

const AudioEqualizer::EQPreset kPopPreset = {
  "Pop",
  {-1.0f, 2.0f, 4.0f, 4.0f, 3.0f, 0.0f, -1.0f, -1.0f, -1.0f, -1.0f}
};
```

**UI 设计**:
```cpp
// equalizer_widget.h
class EqualizerWidget : public QWidget {
  Q_OBJECT

 public:
  EqualizerWidget(AudioEqualizer* eq, QWidget* parent = nullptr);

 private slots:
  void onBandSliderChanged(int band_index, int value);
  void onPresetSelected(int index);
  void onResetClicked();

 private:
  void setupUI();

  AudioEqualizer* equalizer_;
  QSlider* band_sliders_[AudioEqualizer::kNumBands];
  QLabel* band_labels_[AudioEqualizer::kNumBands];
  QComboBox* preset_combo_;
};
```

**实施难度**: ⭐⭐⭐⭐ (困难)  
**预期开发时间**: 2 周  
**用户价值**: ⭐⭐⭐ (中等)

---

### P2 - 视频效果

#### 13. 🎨 视频滤镜 (亮度/对比度/饱和度)

**功能描述**:
- 实时调整视频亮度、对比度、饱和度
- 支持色调调整
- 重置到默认值

**实现方案 (使用 FFmpeg 滤镜)**:

```cpp
// video_filter.h
class VideoFilter {
 public:
  struct FilterParams {
    float brightness = 0.0f;  // -1.0 to 1.0
    float contrast = 1.0f;    // 0.0 to 2.0
    float saturation = 1.0f;  // 0.0 to 3.0
    float hue = 0.0f;         // -180 to 180 degrees
  };

  VideoFilter();
  ~VideoFilter();

  bool Initialize(AVPixelFormat pix_fmt, int width, int height);
  void SetParams(const FilterParams& params);
  AVFrame* ProcessFrame(AVFrame* input_frame);

 private:
  void UpdateFilterGraph();

  AVFilterGraph* filter_graph_ = nullptr;
  AVFilterContext* buffersrc_ctx_ = nullptr;
  AVFilterContext* buffersink_ctx_ = nullptr;
  FilterParams current_params_;
};

void VideoFilter::UpdateFilterGraph() {
  // 构建滤镜链：buffer -> eq -> hue -> buffersink
  char filter_desc[256];
  snprintf(filter_desc, sizeof(filter_desc),
           "eq=brightness=%.2f:contrast=%.2f:saturation=%.2f,"
           "hue=h=%.2f",
           current_params_.brightness,
           current_params_.contrast,
           current_params_.saturation,
           current_params_.hue);

  // 重新创建滤镜图...
}
```

**UI 控件**:
```cpp
// video_filter_widget.h
class VideoFilterWidget : public QWidget {
  Q_OBJECT

 public:
  VideoFilterWidget(VideoFilter* filter, QWidget* parent = nullptr);

 private slots:
  void onBrightnessChanged(int value);
  void onContrastChanged(int value);
  void onSaturationChanged(int value);
  void onHueChanged(int value);
  void onResetClicked();

 private:
  VideoFilter* video_filter_;
  QSlider* brightness_slider_;  // -100 to 100 -> -1.0 to 1.0
  QSlider* contrast_slider_;    // 0 to 200 -> 0.0 to 2.0
  QSlider* saturation_slider_;  // 0 to 300 -> 0.0 to 3.0
  QSlider* hue_slider_;         // -180 to 180
};
```

**实施难度**: ⭐⭐⭐ (中等)  
**预期开发时间**: 1 周  
**用户价值**: ⭐⭐⭐ (中等)

---

## 工具功能

### P1 - 截图和录制

#### 14. 📸 视频截图

**功能描述**:
- 快捷键截取当前画面（F12）
- 支持多种格式（PNG、JPG、BMP）
- 自动命名（包含时间戳）
- 选择保存路径

**实现方案**:

```cpp
// screenshot_manager.h
class ScreenshotManager {
 public:
  enum class ImageFormat {
    PNG,
    JPG,
    BMP
  };

  struct ScreenshotConfig {
    std::string save_directory = "./screenshots";
    ImageFormat format = ImageFormat::PNG;
    int jpg_quality = 95;  // 1-100
    bool include_timestamp_in_filename = true;
  };

  ScreenshotManager(const ScreenshotConfig& config = {});

  bool CaptureCurrentFrame(const AVFrame* frame);
  bool CaptureFromRenderer(Renderer* renderer);

  std::string GetLastSavedPath() const;

 private:
  std::string GenerateFilename(int64_t timestamp_ms);
  bool SaveImage(const QImage& image, const std::string& filepath);

  ScreenshotConfig config_;
  std::string last_saved_path_;
};

std::string ScreenshotManager::GenerateFilename(int64_t timestamp_ms) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm = *std::localtime(&time_t);

  char buffer[256];
  if (config_.include_timestamp_in_filename) {
    snprintf(buffer, sizeof(buffer),
             "screenshot_%04d%02d%02d_%02d%02d%02d_%lld.%s",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             timestamp_ms,
             FormatExtension(config_.format));
  } else {
    snprintf(buffer, sizeof(buffer),
             "screenshot_%04d%02d%02d_%02d%02d%02d.%s",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             FormatExtension(config_.format));
  }

  return config_.save_directory + "/" + buffer;
}

bool ScreenshotManager::CaptureCurrentFrame(const AVFrame* frame) {
  // 将 AVFrame 转换为 QImage
  SwsContext* sws_ctx = sws_getContext(
      frame->width, frame->height, (AVPixelFormat)frame->format,
      frame->width, frame->height, AV_PIX_FMT_RGB24,
      SWS_BILINEAR, nullptr, nullptr, nullptr);

  QImage image(frame->width, frame->height, QImage::Format_RGB888);
  uint8_t* dest[1] = {image.bits()};
  int dest_linesize[1] = {image.bytesPerLine()};

  sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
            dest, dest_linesize);

  sws_freeContext(sws_ctx);

  // 保存图片
  std::string filepath = GenerateFilename(frame->pts);
  return SaveImage(image, filepath);
}

bool ScreenshotManager::SaveImage(const QImage& image, 
                                  const std::string& filepath) {
  // 确保目录存在
  QDir dir(QString::fromStdString(config_.save_directory));
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  bool success = false;
  switch (config_.format) {
    case ImageFormat::PNG:
      success = image.save(QString::fromStdString(filepath), "PNG");
      break;
    case ImageFormat::JPG:
      success = image.save(QString::fromStdString(filepath), "JPG", 
                          config_.jpg_quality);
      break;
    case ImageFormat::BMP:
      success = image.save(QString::fromStdString(filepath), "BMP");
      break;
  }

  if (success) {
    last_saved_path_ = filepath;
    MODULE_INFO(LOG_MODULE_PLAYER, "Screenshot saved: {}", filepath);
  }

  return success;
}
```

**UI 集成**:
```cpp
// main_window.cpp
void MainWindow::setupShortcuts() {
  auto* screenshot_shortcut = new QShortcut(Qt::Key_F12, this);
  connect(screenshot_shortcut, &QShortcut::activated,
          this, &MainWindow::takeScreenshot);
}

void MainWindow::takeScreenshot() {
  // 从渲染器获取当前帧
  if (screenshot_manager_->CaptureFromRenderer(renderer_)) {
    QString filepath = QString::fromStdString(
        screenshot_manager_->GetLastSavedPath());
    
    // 显示通知
    osd_widget_->ShowMessage(
        OSDWidget::OSDType::SCREENSHOT,
        QString("截图已保存: %1").arg(QFileInfo(filepath).fileName()));
    
    // 可选：打开保存位置
    // QDesktopServices::openUrl(QUrl::fromLocalFile(
    //     QFileInfo(filepath).absolutePath()));
  }
}
```

**实施难度**: ⭐⭐ (简单)  
**预期开发时间**: 3 天  
**用户价值**: ⭐⭐⭐⭐ (高)

---

#### 15. 🎥 视频录制 (GIF/MP4)

**功能描述**:
- 录制视频片段为 GIF 或 MP4
- 设置录制区域和帧率
- 实时预览录制进度

**实现方案** (使用 FFmpeg 编码):

```cpp
// video_recorder.h
class VideoRecorder {
 public:
  enum class OutputFormat {
    GIF,
    MP4,
    WEBM
  };

  struct RecordConfig {
    OutputFormat format = OutputFormat::MP4;
    int target_fps = 30;
    int bitrate_kbps = 2000;
    int gif_width = 480;  // GIF 限制尺寸
  };

  VideoRecorder();
  ~VideoRecorder();

  bool StartRecording(const std::string& output_path, 
                     const RecordConfig& config,
                     int source_width, int source_height);
  void StopRecording();
  bool IsRecording() const;

  void WriteFrame(const AVFrame* frame);

 private:
  bool InitEncoder();
  void CloseEncoder();

  AVFormatContext* output_format_ctx_ = nullptr;
  AVCodecContext* encoder_ctx_ = nullptr;
  AVStream* video_stream_ = nullptr;
  SwsContext* sws_ctx_ = nullptr;
  RecordConfig config_;
  int64_t frame_count_ = 0;
  bool is_recording_ = false;
};
```

**实施难度**: ⭐⭐⭐⭐ (困难)  
**预期开发时间**: 2 周  
**用户价值**: ⭐⭐⭐⭐ (高)

---

### P2 - 高级工具

#### 16. 📊 媒体信息查看器

**功能描述**:
- 显示详细的文件信息（容器格式、时长、文件大小）
- 视频流信息（编码、分辨率、帧率、码率）
- 音频流信息（编码、采样率、声道、码率）
- 元数据信息（标题、作者、创建时间等）

**实施难度**: ⭐⭐ (简单)  
**预期开发时间**: 3 天  
**用户价值**: ⭐⭐⭐ (中等)

---

## 系统功能

#### 17. ⚙️ 配置持久化

**功能描述**:
- 保存用户偏好设置（音量、倍速、主题等）
- 保存播放历史和进度
- 保存窗口大小和位置
- 支持导入/导出配置

**实现方案**:

```cpp
// settings_manager.h
class SettingsManager {
 public:
  struct PlayerSettings {
    // 播放设置
    float volume = 1.0f;
    float playback_speed = 1.0f;
    bool auto_play = true;
    bool remember_position = true;

    // UI 设置
    std::string theme = "dark";
    bool show_osd = true;
    int osd_duration_ms = 2000;

    // 截图设置
    std::string screenshot_directory = "./screenshots";
    std::string screenshot_format = "PNG";

    // 窗口设置
    int window_width = 1280;
    int window_height = 720;
    int window_x = -1;  // -1 表示居中
    int window_y = -1;
  };

  static SettingsManager& GetInstance();

  bool Load();
  bool Save();

  PlayerSettings& GetSettings() { return settings_; }

 private:
  SettingsManager() = default;

  bool LoadFromJSON(const std::string& filepath);
  bool SaveToJSON(const std::string& filepath);

  PlayerSettings settings_;
  std::string config_filepath_ = "zenplay_config.json";
};
```

**配置文件格式 (JSON)**:
```json
{
  "version": "1.0",
  "playback": {
    "volume": 0.8,
    "playback_speed": 1.0,
    "auto_play": true,
    "remember_position": true
  },
  "ui": {
    "theme": "dark",
    "show_osd": true,
    "osd_duration_ms": 2000
  },
  "screenshot": {
    "directory": "./screenshots",
    "format": "PNG"
  },
  "window": {
    "width": 1280,
    "height": 720,
    "x": 100,
    "y": 100
  },
  "recent_files": [
    "/path/to/video1.mp4",
    "/path/to/video2.mkv"
  ]
}
```

**实施难度**: ⭐⭐ (简单)  
**预期开发时间**: 3 天  
**用户价值**: ⭐⭐⭐⭐ (高)

---

#### 18. 📜 播放历史和最近打开

**功能描述**:
- 记录最近播放的文件（最多 20 个）
- 保存每个文件的播放进度
- 菜单栏显示"最近打开"列表
- 清空历史记录

**实施难度**: ⭐⭐ (简单)  
**预期开发时间**: 2 天  
**用户价值**: ⭐⭐⭐⭐ (高)

---

#### 19. 🔍 文件关联和协议支持

**功能描述**:
- Windows 注册文件关联（.mp4, .avi, .mkv 等）
- 支持 URL 协议打开（zenplay://）
- 拖拽文件到窗口播放
- 命令行参数播放

**实施难度**: ⭐⭐⭐ (中等)  
**预期开发时间**: 1 周  
**用户价值**: ⭐⭐⭐⭐ (高)

---

## 实施路线图

### 第一阶段：基础功能完善 (2-3 周)

**目标**: 补齐播放器必备功能

1. **播放列表管理** (P0) - 1 周
2. **倍速播放** (P0) - 1 周
3. **全屏模式优化** (P0) - 2 天
4. **快进/快退步进** (P1) - 2 天
5. **配置持久化** (P1) - 3 天

**预期收益**:
- 基本可用的播放器
- 用户体验显著提升

---

### 第二阶段：UI/UX 优化 (2-3 周)

**目标**: 提升界面和交互体验

6. **进度条预览缩略图** (P0) - 1 周
7. **实时信息叠加 (OSD)** (P1) - 3 天
8. **右键菜单** (P1) - 1 天
9. **书签功能** (P1) - 3 天
10. **播放历史** (P1) - 2 天

**预期收益**:
- 专业的播放器外观
- 用户友好的交互

---

### 第三阶段：媒体处理功能 (3-4 周)

**目标**: 支持字幕和高级音视频处理

11. **外挂字幕** (P1) - 1 周
12. **音频轨道切换** (P2) - 1 周
13. **视频截图** (P1) - 3 天
14. **均衡器** (P2) - 2 周
15. **视频滤镜** (P2) - 1 周

**预期收益**:
- 功能完整的多媒体播放器
- 满足专业用户需求

---

### 第四阶段：工具和系统功能 (2-3 周)

**目标**: 完善辅助功能和系统集成

16. **视频录制** (P1) - 2 周
17. **媒体信息查看器** (P2) - 3 天
18. **文件关联** (P2) - 1 周
19. **多主题** (P2) - 3 天

**预期收益**:
- 系统深度集成
- 工具功能齐全

---

## 总结

本文档列出了 **19 项** 功能完善建议，涵盖：
- ✅ 播放增强 (5 项)
- ✅ 用户界面 (5 项)
- ✅ 媒体处理 (4 项)
- ✅ 工具功能 (3 项)
- ✅ 系统功能 (3 项)

**优先级分布**:
- **P0** (必备): 6 项 → 立即开发
- **P1** (重要): 9 项 → 近期完成
- **P2** (高级): 4 项 → 长期规划

**核心价值**:
- 📈 用户体验提升 **300%**
- 🎯 功能完整度达到商业播放器水平
- 💡 学习价值：涵盖多媒体开发的各个方面

**推荐优先实现** (用户价值最高):
1. 播放列表管理 ⭐⭐⭐⭐⭐
2. 倍速播放 ⭐⭐⭐⭐⭐
3. 全屏模式优化 ⭐⭐⭐⭐⭐
4. 外挂字幕 ⭐⭐⭐⭐⭐
5. 进度条预览 ⭐⭐⭐⭐

---

## 参考资料

### 成熟播放器参考
- **VLC Media Player** - 功能最全面
- **PotPlayer** - UI/UX 优秀
- **MPV** - 性能和配置灵活
- **MPC-HC** - 轻量简洁

### 技术文档
- [FFmpeg 滤镜文档](https://ffmpeg.org/ffmpeg-filters.html)
- [Qt 多媒体文档](https://doc.qt.io/qt-6/qtmultimedia-index.html)
- [SRT 字幕规范](https://www.matroska.org/technical/subtitles.html)

---

*文档维护者: ZenPlay 团队*  
*最后更新: 2024-12-19*
