
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
  Result<AVFrame*> ReceiveFrame();

  /**
   * @brief 刷新解码器缓冲区
   */
  bool Flush(std::vector<AVFramePtr>* frames);

  bool opened() const { return opened_; }
  AVMediaType codec_type() const { return codec_type_; }

  void FlushBuffers();

 protected:
  std::unique_ptr<AVCodecContext, AVCodecCtxDeleter> codec_context_;
  AVFramePtr workFrame_ = nullptr;
  AVMediaType codec_type_ = AVMEDIA_TYPE_UNKNOWN;
  bool opened_ = false;
};

}  // namespace zenplay
