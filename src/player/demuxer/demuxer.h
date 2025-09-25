#pragma once

#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

namespace zenplay {

class Demuxer {
 public:
  Demuxer();
  ~Demuxer();

  bool Open(const std::string& url);
  void Close();

  bool ReadPacket(AVPacket** packet);

  bool Seek(int64_t timestamp, bool backward = false);

  AVDictionary* GetMetadata() const;
  int64_t GetDuration() const;  // 返回总时长（毫秒）

  int active_video_stream_index() const { return active_video_stream_index_; }
  int active_audio_stream_index() const { return active_audio_stream_index_; }

  AVStream* findStreamByIndex(int index) const;

 private:
  void probeStreams();

  AVFormatContext* format_context_;
  std::vector<int> video_streams_;
  std::vector<int> audio_streams_;

  int active_video_stream_index_ = -1;
  int active_audio_stream_index_ = -1;

  static std::once_flag init_once_flag_;
};

}  // namespace zenplay
