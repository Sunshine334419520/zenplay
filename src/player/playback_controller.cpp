#include "player/playback_controller.h"

#include "loki/src/bind_util.h"
#include "loki/src/location.h"
#include "player/codec/audio_decoder.h"
#include "player/codec/video_decoder.h"
#include "player/demuxer/demuxer.h"
#include "player/render/renderer.h"

namespace zenplay {

PlaybackController::PlaybackController(Demuxer* demuxer,
                                       VideoDecoder* video_decoder,
                                       AudioDecoder* audio_decoder,
                                       Renderer* renderer)
    : demuxer_(demuxer),
      video_decoder_(video_decoder),
      audio_decoder_(audio_decoder),
      renderer_(renderer) {}

PlaybackController::~PlaybackController() {
  Stop();
}

bool PlaybackController::Start() {
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (is_playing_.load()) {
    return true;  // Already playing
  }

  should_stop_ = false;
  is_playing_ = true;
  is_paused_ = false;

  // 启动解封装线程 - 使用专门的工作线程
  demux_thread_ =
      std::make_unique<std::thread>(&PlaybackController::DemuxTask, this);

  // 启动视频解码线程
  if (video_decoder_ && video_decoder_->opened()) {
    video_decode_thread_ = std::make_unique<std::thread>(
        &PlaybackController::VideoDecodeTask, this);
  }

  // 启动音频解码线程
  if (audio_decoder_ && audio_decoder_->opened()) {
    audio_decode_thread_ = std::make_unique<std::thread>(
        &PlaybackController::AudioDecodeTask, this);
  }

  // 启动渲染任务 - 使用UI线程
  /*
  loki::LokiThread::PostTask(
      loki::ID::UI, LOKI_FROM_HERE,
      loki::BindOnce(&PlaybackController::RenderTask, loki::Unretained(this)));
      */

  return true;
}

void PlaybackController::Stop() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    should_stop_ = true;
    is_playing_ = false;
    is_paused_ = false;
  }

  pause_cv_.notify_all();
  StopAllThreads();
}

void PlaybackController::Pause() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  is_paused_ = true;
}

void PlaybackController::Resume() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    is_paused_ = false;
  }
  pause_cv_.notify_all();
}

bool PlaybackController::Seek(int64_t timestamp) {
  // TODO: 实现seek逻辑
  // 1. 暂停所有解码线程
  // 2. 清空所有队列
  // 3. demuxer seek到指定位置
  // 4. 刷新解码器缓冲
  // 5. 重新开始解码
  return false;
}

void PlaybackController::DemuxTask() {
  AVPacket* packet = nullptr;

  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
      continue;
    }

    // 检查队列大小，避免内存过度使用
    if (video_packet_queue_.Size() > 100 || audio_packet_queue_.Size() > 100) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if (!demuxer_->ReadPacket(&packet)) {
      // 读取失败或到达文件末尾
      break;
    }

    if (!packet) {
      // EOF
      break;
    }

    // 根据流类型分发数据包
    if (packet->stream_index == demuxer_->active_video_stream_index()) {
      video_packet_queue_.Push(packet);
    } else if (packet->stream_index == demuxer_->active_audio_stream_index()) {
      audio_packet_queue_.Push(packet);
    } else {
      av_packet_free(&packet);
    }
  }

  // 发送NULL packet表示流结束
  video_packet_queue_.Push(nullptr);
  audio_packet_queue_.Push(nullptr);
}

void PlaybackController::VideoDecodeTask() {
  if (!video_decoder_ || !video_decoder_->opened()) {
    return;
  }

  AVPacket* packet = nullptr;
  std::vector<AVFramePtr> frames;

  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
      continue;
    }

    // 检查输出队列大小
    if (video_frame_queue_.Size() > 30) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // 从队列获取packet
    if (!video_packet_queue_.Pop(packet)) {
      continue;
    }

    if (!packet) {
      // EOF - flush decoder
      video_decoder_->Flush(&frames);
      for (auto& frame : frames) {
        video_frame_queue_.Push(std::move(frame));
      }
      break;
    }

    // 解码
    if (video_decoder_->Decode(packet, &frames)) {
      for (auto& frame : frames) {
        video_frame_queue_.Push(std::move(frame));
      }
    }

    av_packet_free(&packet);
  }
}

void PlaybackController::AudioDecodeTask() {
  if (!audio_decoder_ || !audio_decoder_->opened()) {
    return;
  }

  AVPacket* packet = nullptr;
  std::vector<AVFramePtr> frames;

  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
      continue;
    }

    // 检查输出队列大小
    if (audio_frame_queue_.Size() > 50) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // 从队列获取packet
    if (!audio_packet_queue_.Pop(packet)) {
      continue;
    }

    if (!packet) {
      // EOF - flush decoder
      audio_decoder_->Flush(&frames);
      for (auto& frame : frames) {
        audio_frame_queue_.Push(std::move(frame));
      }
      break;
    }

    // 解码
    if (audio_decoder_->Decode(packet, &frames)) {
      for (auto& frame : frames) {
        audio_frame_queue_.Push(std::move(frame));
      }
    }

    av_packet_free(&packet);
  }
}

void PlaybackController::RenderTask() {
  const auto frame_duration = std::chrono::milliseconds(33);  // ~30fps
  auto last_render_time = std::chrono::steady_clock::now();

  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
      last_render_time = std::chrono::steady_clock::now();
      continue;
    }

    auto current_time = std::chrono::steady_clock::now();
    auto elapsed = current_time - last_render_time;

    if (elapsed >= frame_duration) {
      AVFramePtr video_frame = nullptr;
      if (video_frame_queue_.Pop(video_frame, std::chrono::milliseconds(5))) {
        if (video_frame && renderer_) {
          // 渲染视频帧
          renderer_->RenderFrame(video_frame.get());
        }
      }  // TODO: 处理音频frame同步
      AVFramePtr audio_frame = nullptr;
      if (audio_frame_queue_.Pop(audio_frame, std::chrono::milliseconds(1))) {
        if (audio_frame) {
          // TODO: 传递给音频输出设备
        }
      }

      last_render_time = current_time;
    }

    // 继续调度渲染任务
    if (!should_stop_.load()) {
      /*
      loki::LokiThread::PostDelayedTask(
          loki::ID::UI, LOKI_FROM_HERE,
          loki::BindOnce(&PlaybackController::RenderTask,
                         loki::Unretained(this)),
          std::chrono::milliseconds(10));
          */
      break;  // 退出当前调用，等待下次调度
    }
  }
}

void PlaybackController::StopAllThreads() {
  // 停止队列
  video_packet_queue_.Stop();
  audio_packet_queue_.Stop();
  video_frame_queue_.Stop();
  audio_frame_queue_.Stop();

  // 等待解封装线程结束
  if (demux_thread_ && demux_thread_->joinable()) {
    demux_thread_->join();
    demux_thread_.reset();
  }

  // 等待解码线程结束
  if (video_decode_thread_ && video_decode_thread_->joinable()) {
    video_decode_thread_->join();
    video_decode_thread_.reset();
  }

  if (audio_decode_thread_ && audio_decode_thread_->joinable()) {
    audio_decode_thread_->join();
    audio_decode_thread_.reset();
  }

  // 清空队列
  video_packet_queue_.Clear();
  audio_packet_queue_.Clear();
  video_frame_queue_.Clear();
  audio_frame_queue_.Clear();
}

}  // namespace zenplay
