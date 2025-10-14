#pragma once

#include <chrono>
#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

namespace zenplay {

struct AVFrameDeleter {
  void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

/**
 * @brief 媒体帧时间戳信息 (音频和视频通用)
 */
struct MediaTimestamp {
  int64_t pts = AV_NOPTS_VALUE;      // 显示时间戳
  int64_t dts = AV_NOPTS_VALUE;      // 解码时间戳
  AVRational time_base{1, 1000000};  // 时间基准

  // 转换为毫秒
  double ToMilliseconds() const {
    if (pts == AV_NOPTS_VALUE || pts < 0) {
      return -1.0;  // 返回-1表示无效时间戳
    }
    return pts * av_q2d(time_base) * 1000.0;
  }

  // 转换为秒
  double ToSeconds() const {
    if (pts == AV_NOPTS_VALUE || pts < 0) {
      return -1.0;  // 返回-1表示无效时间戳
    }
    return pts * av_q2d(time_base);
  }
};

/**
 * @brief 媒体帧信息 (音频和视频通用)
 */
struct MediaFrame {
  AVFramePtr frame;                                    // FFmpeg 解码后的帧
  MediaTimestamp timestamp;                            // 时间戳信息
  std::chrono::steady_clock::time_point receive_time;  // 接收时间

  MediaFrame(AVFramePtr f, const MediaTimestamp& ts)
      : frame(std::move(f)),
        timestamp(ts),
        receive_time(std::chrono::steady_clock::now()) {}
};

}  // namespace zenplay
