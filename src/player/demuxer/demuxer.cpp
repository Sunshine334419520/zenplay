#include "player/demuxer/demuxer.h"

#include "demuxer.h"
#include "player/common/ffmpeg_error_utils.h"
#include "player/common/log_manager.h"

namespace zenplay {

std::once_flag Demuxer::init_once_flag_;

Demuxer::Demuxer() : format_context_(nullptr) {
  std::call_once(init_once_flag_, []() { avformat_network_init(); });
}

Demuxer::~Demuxer() {
  Close();
}

Result<void> Demuxer::Open(const std::string& url) {
  if (format_context_) {
    Close();
  }

  AVDictionary* options = nullptr;

  // ✅ 通用网络选项（所有网络流）
  av_dict_set(&options, "reconnect", "1", 0);
  av_dict_set(&options, "reconnect_delay_max", "5", 0);
  av_dict_set(&options, "reconnect_streamed", "1", 0);

  // ✅ HTTP/HTTPS 优化
  if (url.find("http://") == 0 || url.find("https://") == 0) {
    av_dict_set(&options, "buffer_size", "10485760", 0);  // 10MB
    av_dict_set(&options, "max_delay", "5000000", 0);     // 5s
    MODULE_DEBUG(LOG_MODULE_DEMUXER,
                 "HTTP(S) stream: buffer=10MB, max_delay=5s");
  }
  // ✅ RTSP/RTMP 优化
  else if (url.find("rtsp://") == 0 || url.find("rtmp://") == 0) {
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "buffer_size", "5242880", 0);  // 5MB
    av_dict_set(&options, "max_delay", "5000000", 0);    // 5s
    av_dict_set(&options, "timeout", "2000000", 0);      // 2s超时
    MODULE_DEBUG(LOG_MODULE_DEMUXER, "RTSP(P) stream: buffer=5MB, timeout=2s");
  }
  // ✅ UDP 协议（低延迟直播）
  else if (url.find("udp://") == 0) {
    av_dict_set(&options, "buffer_size", "1048576", 0);  // 1MB
    av_dict_set(&options, "timeout", "1000000", 0);      // 1s
    MODULE_DEBUG(LOG_MODULE_DEMUXER, "UDP stream: buffer=1MB, timeout=1s");
  }

  int ret =
      avformat_open_input(&format_context_, url.c_str(), nullptr, &options);
  if (ret < 0) {
    av_dict_free(&options);
    avformat_free_context(format_context_);
    format_context_ = nullptr;
    return FFmpegErrorToResult(ret, "Open input: " + url);
  }

  av_dict_free(&options);

  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    Close();
    return FFmpegErrorToResult(ret, "Find stream info: " + url);
  }

  probeStreams();
  return Result<void>::Ok();
}

void Demuxer::Close() {
  if (format_context_) {
    avformat_free_context(format_context_);
    format_context_ = nullptr;
    video_streams_.clear();
    audio_streams_.clear();
    active_video_stream_index_ = -1;
    active_audio_stream_index_ = -1;
  }
}

Result<AVPacket*> Demuxer::ReadPacket() {
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    return Result<AVPacket*>::Err(ErrorCode::kOutOfMemory,
                                  "Failed to allocate AVPacket");
  }

  int ret = av_read_frame(format_context_, packet);

  if (ret == AVERROR_EOF) {
    av_packet_free(&packet);
    // EOF 不是错误，返回 nullptr 表示结束
    return Result<AVPacket*>::Ok(nullptr);
  } else if (ret < 0) {
    av_packet_free(&packet);
    return Result<AVPacket*>::Err(MapFFmpegError(ret),
                                  FormatFFmpegError(ret, "Read packet"));
  }

  // 跳过非活动流的数据包
  if (packet->stream_index != active_audio_stream_index_ &&
      packet->stream_index != active_video_stream_index_) {
    av_packet_unref(packet);
    av_packet_free(&packet);
    return ReadPacket();  // 递归读取下一个数据包
  }

  // ✅ 添加调试日志：输出 demuxer 读取的 packet PTS/DTS
  if (packet->stream_index == active_video_stream_index_) {
    AVStream* stream = format_context_->streams[packet->stream_index];
    double pts_ms = packet->pts != AV_NOPTS_VALUE
                        ? packet->pts * av_q2d(stream->time_base) * 1000.0
                        : -1.0;
    double dts_ms = packet->dts != AV_NOPTS_VALUE
                        ? packet->dts * av_q2d(stream->time_base) * 1000.0
                        : -1.0;

    // MODULE_DEBUG(LOG_MODULE_DEMUXER,
    //              "📦 Demux video packet: pts={}, dts={}, reorder_offset={}, "
    //              "pts_ms={:.2f}, dts_ms={:.2f}, size={}, flags={}",
    //              packet->pts, packet->dts, (packet->pts - packet->dts),
    //              pts_ms, dts_ms, packet->size, packet->flags);
  }

  return Result<AVPacket*>::Ok(packet);
}

bool Demuxer::Seek(int64_t timestamp, bool backward) {
  if (!format_context_) {
    return false;  // Not opened
  }

  int ret = av_seek_frame(format_context_, -1, timestamp,
                          backward ? AVSEEK_FLAG_BACKWARD : 0);

  if (ret < 0) {
    return false;  // Seek failed
  }

  // After seeking, we need to flush the codec buffers
  avformat_flush(format_context_);

  return true;  // Seek successful
}

AVDictionary* Demuxer::GetMetadata() const {
  if (!format_context_) {
    return nullptr;  // Not opened
  }
  return format_context_->metadata;  // Return the metadata dictionary
}

int64_t Demuxer::GetDuration() const {
  if (!format_context_) {
    return 0;  // Not opened
  }
  // 返回毫秒：FFmpeg的duration是微秒，AV_TIME_BASE=1000000
  return static_cast<int64_t>(format_context_->duration / 1000);  // 转换为毫秒
}

AVStream* Demuxer::findStreamByIndex(int index) const {
  if (!format_context_ || index < 0 ||
      index >= static_cast<int>(format_context_->nb_streams)) {
    return nullptr;  // Invalid index or not opened
  }
  return format_context_->streams[index];
}

void Demuxer::probeStreams() {
  video_streams_.clear();
  audio_streams_.clear();

  for (unsigned int i = 0; i < format_context_->nb_streams; ++i) {
    AVStream* stream = format_context_->streams[i];
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_streams_.push_back(i);
      if (active_video_stream_index_ == -1) {
        active_video_stream_index_ = i;
      }
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_streams_.push_back(i);
      if (active_audio_stream_index_ == -1) {
        active_audio_stream_index_ = i;
      }
    }
  }

  MODULE_INFO(LOG_MODULE_DEMUXER, "Found {} video streams, {} audio streams",
              video_streams_.size(), audio_streams_.size());
}

}  // namespace zenplay
