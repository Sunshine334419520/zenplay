#include "player/demuxer/demuxer.h"

#include "demuxer.h"

namespace zenplay {
Demuxer::Demuxer() : format_context_(nullptr) {
  std::call_once(init_once_flag_, []() { avformat_network_init(); });
}

Demuxer::~Demuxer() {
  Close();
}

bool Demuxer::Open(const std::string& url) {
  if (format_context_) {
    Close();
  }

  AVDictionary* options = nullptr;

  if (url.find("rtsp://") == 0 || url.find("rtmp://") == 0) {
    // For RTSP/RTMP, we can set some options if needed
    av_dict_set(&options, "rtsp_transport", "tcp", 0);  // Use TCP for RTSP
    av_dict_set(&options, "timeout", "5000000", 0);  // Set timeout to 5 seconds
  }

  int ret =
      avformat_open_input(&format_context_, url.c_str(), nullptr, &options);
  if (ret < 0) {
    avformat_free_context(format_context_);
    format_context_ = nullptr;
    return false;
  }

  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    Close();
    return false;
  }

  probeStreams();
  int32_t test = format_context_->duration;
  return true;
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

bool Demuxer::ReadPacket(AVPacket** packetRet) {
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    return false;  // Failed to allocate packet
  }

  int ret = av_read_frame(format_context_, packet);

  if (ret == AVERROR_EOF) {
    av_packet_free(&packet);
    *packetRet = nullptr;  // End of file, no more packets to read
    return true;
  } else if (ret < 0) {
    av_packet_free(&packet);
    return false;  // Error reading packet
  }

  if (packet->stream_index != active_audio_stream_index_ &&
      packet->stream_index != active_video_stream_index_) {
    av_packet_unref(packet);
    return ReadPacket(packetRet);  // Read next packet if this one is not from
                                   // an active stream
  }

  *packetRet = packet;  // Return the packet
  return true;
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

int Demuxer::GetDuration() const {
  if (!format_context_) {
    return 0;  // Not opened
  }
  return static_cast<int>(format_context_->duration /
                          AV_TIME_BASE);  // Duration in seconds
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
}

}  // namespace zenplay
