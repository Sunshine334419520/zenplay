#include "player/codec/decode.h"

namespace zenplay {

Decoder::Decoder() {}

Decoder::~Decoder() {}

bool Decoder::Open(AVCodecParameters* codec_params, AVDictionary** options) {
  if (opened_) {
    return false;
  }

  const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
  if (!codec) {
    return false;  // Codec not found
  }

  AVCodecContext* raw = avcodec_alloc_context3(codec);
  if (!codec_context_) {
    return false;  // Failed to allocate codec context
  }
  codec_context_.reset(raw);

  int ret = avcodec_parameters_to_context(codec_context_.get(), codec_params);
  if (ret < 0) {
    codec_context_.reset();  // Reset the context on failure
    return false;            // Failed to copy codec parameters
  }

  ret = avcodec_open2(codec_context_.get(), codec, options);
  if (ret < 0) {
    codec_context_.reset();  // Reset the context on failure
    return false;            // Failed to open codec
  }

  workFrame_.reset(av_frame_alloc());
  opened_ = true;
  codec_type_ = codec_params->codec_type;
  return true;  // Successfully opened
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

  int ret = avcodec_send_packet(codec_context_.get(), packet);
  if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
    return false;  // Error sending packet to decoder
  }

  frames->clear();  // Clear previous frames
  while (true) {
    ret = avcodec_receive_frame(codec_context_.get(), workFrame_.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;  // No more frames to receive
    } else if (ret < 0) {
      return false;  // Error receiving frame
    }

    // clone the frame to ensure it is not modified by subsequent calls
    AVFrame* clone = av_frame_clone(workFrame_.get());
    if (!clone) {
      av_frame_unref(workFrame_.get());
      return false;  // Failed to clone frame
    }

    frames->emplace_back(
        AVFramePtr(clone));  // Add cloned frame to output vector
  }

  return true;
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
