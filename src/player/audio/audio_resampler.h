#pragma once

#include <memory>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include "player/audio/resampled_audio_frame.h"
#include "player/common/common_def.h"

namespace zenplay {

/**
 * @brief 音频重采样器
 *
 * 职责：
 * - 管理 SwrContext 生命周期
 * - 执行音频格式转换（采样率、声道数、采样格式）
 * - 重用缓冲区以避免内存分配
 * - 提供线程安全的重采样操作
 *
 * 设计原则：
 * - 单一职责：仅负责重采样，不管理播放队列
 * - 延迟初始化：从第一个 AVFrame 获取源格式
 * - 缓冲区重用：resampled_buffer_ 仅分配一次
 * - 可测试性：可独立单元测试
 */
class AudioResampler {
 public:
  /**
   * @brief 音频重采样配置
   */
  struct ResamplerConfig {
    int target_sample_rate = 44100;                    // 目标采样率
    int target_channels = 2;                           // 目标声道数
    AVSampleFormat target_format = AV_SAMPLE_FMT_S16;  // 目标采样格式
    int target_bits_per_sample = 16;                   // 目标位深度

    // 性能优化选项
    bool enable_simd = true;  // 启用 SIMD 优化（默认开启）

    /**
     * @brief 获取每采样字节数
     */
    int GetBytesPerSample() const {
      return target_channels * (target_bits_per_sample / 8);
    }
  };

  AudioResampler();
  ~AudioResampler();

  // 禁止拷贝和赋值
  AudioResampler(const AudioResampler&) = delete;
  AudioResampler& operator=(const AudioResampler&) = delete;

  /**
   * @brief 初始化重采样器配置
   * @param config 目标音频配置
   * @note 仅设置目标配置，SwrContext 会在第一次 Resample 时延迟初始化
   */
  void SetConfig(const ResamplerConfig& config);

  /**
   * @brief 获取当前配置
   */
  const ResamplerConfig& GetConfig() const { return config_; }

  /**
   * @brief 重采样音频帧
   * @param frame 源音频帧（AVFrame*）
   * @param timestamp 时间戳信息
   * @param out_resampled 输出：重采样后的帧
   * @return 成功返回 true
   *
   * @note 线程安全：可在解码线程中调用
   * @note 延迟初始化：首次调用时会初始化 SwrContext
   * @note 缓冲区重用：避免频繁内存分配
   * @note 智能优化：如果源格式==目标格式，跳过重采样（零拷贝）
   */
  bool Resample(const AVFrame* frame,
                const MediaTimestamp& timestamp,
                ResampledAudioFrame& out_resampled);

  /**
   * @brief 检查源格式是否与目标格式匹配（无需重采样）
   * @param frame 源音频帧
   * @return 如果格式匹配返回 true
   */
  bool IsFormatMatching(const AVFrame* frame) const;

  /**
   * @brief 检查是否已初始化
   */
  bool IsInitialized() const { return initialized_; }

  /**
   * @brief 获取源音频格式信息
   */
  struct SourceFormat {
    int sample_rate = 0;
    int channels = 0;
    AVSampleFormat format = AV_SAMPLE_FMT_NONE;
  };
  SourceFormat GetSourceFormat() const;

  /**
   * @brief 重置重采样器（用于 Seek 后清理状态）
   */
  void Reset();

  /**
   * @brief 清理资源
   */
  void Cleanup();

 private:
  /**
   * @brief 初始化 SwrContext（延迟初始化，从第一帧获取源格式）
   * @param frame 第一个音频帧
   * @return 成功返回 true
   */
  bool InitializeSwrContext(const AVFrame* frame);

  /**
   * @brief 执行实际的重采样操作
   * @param frame 源帧
   * @param out_resampled 输出帧
   * @return 成功返回 true
   */
  bool DoResample(const AVFrame* frame, ResampledAudioFrame& out_resampled);

  /**
   * @brief 零拷贝复制（源格式 == 目标格式时）
   * @param frame 源帧
   * @param timestamp 时间戳
   * @param out_resampled 输出帧
   * @return 成功返回 true
   */
  bool CopyFrameWithoutResampling(const AVFrame* frame,
                                  const MediaTimestamp& timestamp,
                                  ResampledAudioFrame& out_resampled);

 private:
  // 配置
  ResamplerConfig config_;

  // SwrContext（FFmpeg 重采样上下文）
  SwrContext* swr_context_ = nullptr;

  // 源音频格式（从第一帧延迟初始化）
  int src_sample_rate_ = 0;
  int src_channels_ = 0;
  AVSampleFormat src_format_ = AV_SAMPLE_FMT_NONE;
  bool initialized_ = false;

  // 重采样缓冲区（重用以避免频繁分配）
  std::vector<uint8_t> resampled_buffer_;
};

}  // namespace zenplay
