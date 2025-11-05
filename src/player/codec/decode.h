
#pragma once

#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include "player/common/common_def.h"
#include "player/common/error.h"

namespace zenplay {

struct AVCodecCtxDeleter {
  void operator()(AVCodecContext* ctx) const {
    if (ctx) {
      AVCodecContext* tmp = ctx;
      avcodec_free_context(&tmp);
    }
  }
};

class Decoder {
 public:
  Decoder();
  virtual ~Decoder();

  /**
   * @brief 打开解码器
   * @param codec_params 编解码器参数
   * @param options 解码器选项（可选）
   * @return Result<void> 成功返回 Ok()，失败返回详细错误
   */
  Result<void> Open(AVCodecParameters* codec_params,
                    AVDictionary** options = nullptr);

  /**
   * @brief 关闭解码器并释放资源
   */
  void Close();

  /**
   * @brief 解码数据包并接收所有可用帧
   * @param packet 待解码的数据包
   * @param frames 输出帧列表
   * @return 成功返回 true
   */
  bool Decode(AVPacket* packet, std::vector<AVFramePtr>* frames);

  /**
   * @brief 接收单个解码后的帧
   * @return Result<AVFrame*> 成功返回帧指针，EAGAIN 返回 nullptr，失败返回错误
   */
  virtual Result<AVFrame*> ReceiveFrame();

  /**
   * @brief 刷新解码器缓冲区
   */
  bool Flush(std::vector<AVFramePtr>* frames);

  bool opened() const { return opened_; }
  AVMediaType codec_type() const { return codec_type_; }

  /**
   * @brief 获取底层 AVCodecContext（用于高级操作）
   * @return AVCodecContext* 或 nullptr（未打开时）
   */
  AVCodecContext* GetCodecContext() const { return codec_context_.get(); }

  void FlushBuffers();

 protected:
  /**
   * @brief 配置解码器的钩子函数（在 avcodec_open2 之前调用）
   *
   * 子类可以重写此函数来配置硬件加速等特殊参数。
   * 例如：VideoDecoder 可以在此设置 hw_device_ctx 和 hw_frames_ctx
   *
   * @param codec_ctx 已分配但尚未打开的解码器上下文
   * @return Result<void> 成功返回 Ok()，失败返回错误
   */
  virtual Result<void> OnBeforeOpen(AVCodecContext* codec_ctx) {
    // 默认实现：什么都不做
    return Result<void>::Ok();
  }

  std::unique_ptr<AVCodecContext, AVCodecCtxDeleter> codec_context_;
  AVFramePtr workFrame_ = nullptr;
  AVMediaType codec_type_ = AVMEDIA_TYPE_UNKNOWN;
  bool opened_ = false;
};

}  // namespace zenplay
