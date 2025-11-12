#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "player/common/error.h"

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

  /**
   * @brief 打开媒体文件或流
   * @param url 文件路径或网络 URL
   * @return Result<void> 成功返回 Ok()，失败返回详细错误信息
   */
  Result<void> Open(const std::string& url);

  /**
   * @brief 关闭 Demuxer 并释放资源
   */
  void Close();

  /**
   * @brief 读取下一个数据包
   * @return Result<AVPacket*> 成功返回数据包指针，EOF 返回
   * nullptr，失败返回错误
   */
  Result<AVPacket*> ReadPacket();

  /**
   * @brief 跳转到指定时间戳
   * @param timestamp 目标时间戳（微秒）
   * @param backward 是否向后搜索关键帧
   * @return 成功返回 true，失败返回 false
   */
  bool Seek(int64_t timestamp, bool backward = false);

  AVDictionary* GetMetadata() const;
  int64_t GetDuration() const;  // 返回总时长（毫秒）

  int active_video_stream_index() const { return active_video_stream_index_; }
  int active_audio_stream_index() const { return active_audio_stream_index_; }

  AVStream* findStreamByIndex(int index) const;

 private:
  void probeStreams();
  bool IsNetworkProtocol(const std::string& url) const;

  AVFormatContext* format_context_;
  std::vector<int> video_streams_;
  std::vector<int> audio_streams_;

  int active_video_stream_index_ = -1;
  int active_audio_stream_index_ = -1;

  static std::once_flag init_once_flag_;
};

}  // namespace zenplay
