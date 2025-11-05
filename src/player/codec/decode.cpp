#include "player/codec/decode.h"

#include "player/common/ffmpeg_error_utils.h"
#include "player/common/log_manager.h"

namespace zenplay {

Decoder::Decoder() {}

Decoder::~Decoder() {}

Result<void> Decoder::Open(AVCodecParameters* codec_params,
                           AVDictionary** options) {
  if (opened_) {
    return Result<void>::Err(ErrorCode::kAlreadyRunning,
                             "Decoder is already opened");
  }

  if (!codec_params) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "codec_params is null");
  }

  const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
  if (!codec) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Codec not found");
    return Result<void>::Err(ErrorCode::kDecoderNotFound,
                             "Codec not found for codec_id: " +
                                 std::to_string(codec_params->codec_id));
  }

  AVCodecContext* raw = avcodec_alloc_context3(codec);
  if (!raw) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Failed to allocate codec context");
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate codec context");
  }
  codec_context_.reset(raw);

  int ret = avcodec_parameters_to_context(codec_context_.get(), codec_params);
  if (ret < 0) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Failed to copy codec parameters");
    codec_context_.reset();
    return FFmpegErrorToResult(ret, "Copy codec parameters");
  }

  // ✅ 调用钩子函数：让子类在 avcodec_open2 之前配置（例如硬件加速）
  auto hook_result = OnBeforeOpen(codec_context_.get());
  if (!hook_result.IsOk()) {
    MODULE_ERROR(LOG_MODULE_DECODER, "OnBeforeOpen failed: {}",
                 hook_result.Message());
    codec_context_.reset();
    return hook_result;
  }

  ret = avcodec_open2(codec_context_.get(), codec, options);
  if (ret < 0) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Failed to open codec");
    codec_context_.reset();
    return FFmpegErrorToResult(ret, "Open codec");
  }

  workFrame_.reset(av_frame_alloc());
  if (!workFrame_) {
    codec_context_.reset();
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate AVFrame");
  }

  opened_ = true;
  codec_type_ = codec_params->codec_type;
  return Result<void>::Ok();
}

void Decoder::Close() {
  if (opened_) {
    codec_context_.reset();
    workFrame_.reset();
    opened_ = false;
    codec_type_ = AVMEDIA_TYPE_UNKNOWN;
  }
}

bool zenplay::Decoder::Decode(AVPacket* packet,
                              std::vector<AVFramePtr>* frames) {
  if (!opened_) {
    return false;  // Decoder not opened
  }

  frames->clear();  // Clear previous frames first

  // ✅ 发送 packet 到解码器
  int ret = avcodec_send_packet(codec_context_.get(), packet);

  if (ret < 0) {
    if (ret == AVERROR(EAGAIN)) {
      // 解码器缓冲区满，需要先接收帧
      // 这是正常情况，不记录错误
    } else if (ret == AVERROR_EOF) {
      // EOF，正常情况
    } else {
      // ✅ CRITICAL: Don't return immediately on send error!
      // AVERROR_INVALIDDATA is NORMAL for B-frame streams due to packet
      // reordering. The decoder will buffer these packets and decode them when
      // reference frames arrive. This matches MPV's behavior (see mpv
      // f_decoder_wrapper.c:1343-1347).

      // Only log as DEBUG for INVALIDDATA (expected with B-frames), WARN for
      // others
      if (ret == AVERROR_INVALIDDATA) {
        MODULE_DEBUG(LOG_MODULE_DECODER,
                     "B-frame packet buffered (AVERROR_INVALIDDATA), waiting "
                     "for references");
      } else {
        MODULE_WARN(
            LOG_MODULE_DECODER,
            "avcodec_send_packet failed: {} (error code: {}), will still "
            "try to receive frames",
            FormatFFmpegError(ret, "send_packet"), ret);
      }
      // ✅ Continue to receive frames from internal buffer
    }
  }

  // ✅ 接收所有可用的帧
  // IMPORTANT: Always attempt to receive frames, even if send_packet failed!
  while (true) {
    ret = avcodec_receive_frame(codec_context_.get(), workFrame_.get());

    if (ret == AVERROR(EAGAIN)) {
      // 需要更多数据，正常退出
      break;
    } else if (ret == AVERROR_EOF) {
      // EOF，正常退出
      break;
    } else if (ret < 0) {
      // ✅ 记录接收帧失败的详细错误
      MODULE_ERROR(LOG_MODULE_DECODER,
                   "avcodec_receive_frame failed: {} (error code: {})",
                   FormatFFmpegError(ret, "receive_frame"), ret);
      return false;
    }

    // ✅ CRITICAL FIX: Don't use av_frame_clone() for hardware frames!
    // av_frame_clone() increments hardware surface refcount without creating
    // new surfaces, causing pool exhaustion when decoder needs DPB references.
    // Instead, transfer ownership of workFrame to output (like MPV does).

    // Transfer ownership: workFrame_'s buffer refs → new AVFrame
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
      av_frame_unref(workFrame_.get());
      MODULE_ERROR(LOG_MODULE_DECODER, "Failed to allocate frame");
      return false;
    }

    // Move buffer references (not clone!)
    av_frame_move_ref(frame, workFrame_.get());

    frames->emplace_back(AVFramePtr(frame));
  }

  return true;
}

Result<AVFrame*> zenplay::Decoder::ReceiveFrame() {
  if (!opened_) {
    return Result<AVFrame*>::Err(ErrorCode::kNotInitialized,
                                 "Decoder not opened");
  }

  int ret = avcodec_receive_frame(codec_context_.get(), workFrame_.get());

  if (ret == AVERROR(EAGAIN)) {
    // Need more data, not an error
    return Result<AVFrame*>::Ok(nullptr);
  } else if (ret == AVERROR_EOF) {
    // EOF reached
    return Result<AVFrame*>::Ok(nullptr);
  } else if (ret < 0) {
    return Result<AVFrame*>::Err(MapFFmpegError(ret),
                                 FormatFFmpegError(ret, "Receive frame"));
  }

  // Clone the frame to ensure it's not modified by subsequent calls
  AVFrame* clone = av_frame_clone(workFrame_.get());
  if (!clone) {
    av_frame_unref(workFrame_.get());
    return Result<AVFrame*>::Err(ErrorCode::kOutOfMemory,
                                 "Failed to clone AVFrame");
  }

  return Result<AVFrame*>::Ok(clone);
}

bool zenplay::Decoder::Flush(std::vector<AVFramePtr>* frames) {
  return Decode(nullptr, frames);  // Send a null packet to flush the decoder
}

void zenplay::Decoder::FlushBuffers() {
  if (codec_context_) {
    avcodec_flush_buffers(codec_context_.get());  // Flush the codec buffers
  }
}

}  // namespace zenplay
