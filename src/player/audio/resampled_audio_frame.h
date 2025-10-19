#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zenplay {

/**
 * @brief 重采样后的音频帧
 *
 * 表示已经过重采样处理的PCM音频数据，可直接用于播放
 * 设计目标：
 * - 在解码线程中创建并填充
 * - 在音频回调中快速消费（仅memcpy）
 * - 避免音频回调中的CPU密集操作
 */
struct ResampledAudioFrame {
  /**
   * @brief PCM音频数据
   * 格式：交错存储的PCM样本
   * 例如：立体声S16 = [L0, R0, L1, R1, L2, R2, ...]
   */
  std::vector<uint8_t> pcm_data;

  /**
   * @brief PTS（毫秒）
   * 用于音视频同步
   */
  int64_t pts_ms = 0;

  /**
   * @brief 采样数
   * 表示有多少个音频采样点（不是字节数）
   */
  int sample_count = 0;

  /**
   * @brief 采样率（Hz）
   */
  int sample_rate = 0;

  /**
   * @brief 声道数
   */
  int channels = 0;

  /**
   * @brief 每个采样的字节数
   * 例如：S16 = 2字节，S32 = 4字节
   */
  int bytes_per_sample = 0;

  /**
   * @brief 获取PCM数据总字节数
   */
  size_t GetDataSize() const { return pcm_data.size(); }

  /**
   * @brief 获取音频时长（毫秒）
   */
  double GetDurationMs() const {
    if (sample_rate <= 0) {
      return 0.0;
    }
    return (sample_count * 1000.0) / sample_rate;
  }

  /**
   * @brief 清空数据（释放内存）
   */
  void Clear() {
    pcm_data.clear();
    pcm_data.shrink_to_fit();
    pts_ms = 0;
    sample_count = 0;
  }

  /**
   * @brief 预分配内存
   * @param size 字节数
   */
  void Reserve(size_t size) { pcm_data.reserve(size); }

  /**
   * @brief 检查是否为空
   */
  bool IsEmpty() const { return pcm_data.empty(); }
};

}  // namespace zenplay
