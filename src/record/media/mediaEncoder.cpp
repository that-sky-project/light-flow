#include "mediaEncoder.hpp"

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

std::string AvError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(code, buffer, sizeof(buffer));
  return buffer;
}

bool StartsWith(const std::string &value, const char *prefix) {
  return value.compare(0, strlen(prefix), prefix) == 0;
}

bool EndsWith(const std::string &value, const char *suffix) {
  usize length = strlen(suffix);
  return value.size() >= length
      && value.compare(value.size() - length, length, suffix) == 0;
}

const char *GuessMuxer(const std::string &url, bool &isNetwork) {
  isNetwork = url.find("://") != std::string::npos;

  if (StartsWith(url, "rtmp://") || StartsWith(url, "rtmps://"))
    return "flv";
  if (StartsWith(url, "udp://") || StartsWith(url, "tcp://") ||
      StartsWith(url, "srt://"))
    return "mpegts";

  if (EndsWith(url, ".mkv"))
    return "matroska";
  if (EndsWith(url, ".mov"))
    return "mov";
  if (EndsWith(url, ".flv"))
    return "flv";
  if (EndsWith(url, ".ts"))
    return "mpegts";

  return "mp4";
}

AVPixelFormat ToAvPixelFormat(SourcePixelFormat format) {
  switch (format) {
    case kSourcePixelFormat_RGBA8:
      return AV_PIX_FMT_RGBA;
    case kSourcePixelFormat_BGRA8:
      return AV_PIX_FMT_BGRA;
    case kSourcePixelFormat_RGB10A2:
      return AV_PIX_FMT_X2RGB10LE;
    case kSourcePixelFormat_BGR10A2:
      return AV_PIX_FMT_X2BGR10LE;
    case kSourcePixelFormat_RGBAF16:
      return AV_PIX_FMT_RGBAF16LE;
    case kSourcePixelFormat_RGBAF32:
      return AV_PIX_FMT_RGBAF32LE;
    default:
      return AV_PIX_FMT_NONE;
  }
}

} // namespace

MediaEncoder::~MediaEncoder() {
  Close();
}

void MediaEncoder::GlobalInitialize() {
  static bool initialized = false;

  if (!initialized) {
    avformat_network_init();
    av_log_set_level(AV_LOG_ERROR);
    initialized = true;
  }
}

void MediaEncoder::GlobalShutdown() {
  // Deliberately left empty: avformat_network_deinit() would break a second
  // capture session if the mod is disabled and re-enabled.
}

bool MediaEncoder::Open(
  const std::string &url,
  i32 width,
  i32 height,
  i32 fps,
  i32 bitrateKbps,
  const std::string &codec,
  const AudioStreamConfig *audio,
  std::string &error
) {
  Close();

  if (url.empty()) {
    error = "output path is empty";
    return false;
  }

  GlobalInitialize();

  m_width = std::max(2, width & ~1);
  m_height = std::max(2, height & ~1);
  m_fps = fps > 0 ? fps : 30;
  m_url = url;
  m_lastPts = -1;
  m_headerWritten = false;
  m_scalerWidth = 0;
  m_scalerHeight = 0;
  m_scalerFormat = kSourcePixelFormat_Unknown;

  bool isNetwork = false;
  const char *muxerName = GuessMuxer(url, isNetwork);

  AVFormatContext *formatCtx = nullptr;
  int result = avformat_alloc_output_context2(
    &formatCtx, nullptr, muxerName, url.c_str());
  if (result < 0 || !formatCtx) {
    error = "cannot create output context (" + std::string(muxerName) +
            "): " + AvError(result);
    return false;
  }
  m_formatCtx = formatCtx;

  const AVCodec *encoder = nullptr;
  if (!codec.empty())
    encoder = avcodec_find_encoder_by_name(codec.c_str());
  if (!encoder)
    encoder = avcodec_find_encoder_by_name("libx264");
  if (!encoder)
    encoder = avcodec_find_encoder_by_name("h264_mf");
  if (!encoder)
    encoder = avcodec_find_encoder_by_name("libopenh264");
  if (!encoder)
    encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!encoder)
    encoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
  if (!encoder) {
    error = "no H.264/MPEG-4 encoder is available in avcodec";
    Close();
    return false;
  }

  m_codecName = encoder->name;

  AVStream *stream = avformat_new_stream(formatCtx, nullptr);
  if (!stream) {
    error = "cannot create output stream";
    Close();
    return false;
  }
  m_stream = stream;

  AVCodecContext *codecCtx = avcodec_alloc_context3(encoder);
  if (!codecCtx) {
    error = "cannot allocate codec context";
    Close();
    return false;
  }
  m_codecCtx = codecCtx;

  // H.264 and MPEG-4 encoders accept 8-bit 4:2:0 universally. More exotic
  // encoders (h264_mf) may reject it; the UI lets the user pick another codec.
  AVPixelFormat pixelFormat = AV_PIX_FMT_YUV420P;
  m_pixFmt = pixelFormat;

  codecCtx->width = m_width;
  codecCtx->height = m_height;
  codecCtx->time_base = AVRational{1, 1000};
  codecCtx->framerate = AVRational{m_fps, 1};
  codecCtx->pix_fmt = pixelFormat;
  codecCtx->bit_rate = static_cast<int64_t>(std::max(100, bitrateKbps)) * 1000;
  codecCtx->gop_size = m_fps * 2;
  codecCtx->max_b_frames = 0;
  codecCtx->thread_count = 0;

  if (formatCtx->oformat->flags & AVFMT_GLOBALHEADER)
    codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  if (strcmp(encoder->name, "libx264") == 0) {
    av_opt_set(codecCtx->priv_data, "preset", "veryfast", 0);
    if (isNetwork)
      av_opt_set(codecCtx->priv_data, "tune", "zerolatency", 0);
    av_opt_set_int(codecCtx->priv_data, "forced-idr", 1, 0);
  }

  result = avcodec_open2(codecCtx, encoder, nullptr);
  if (result < 0) {
    error = "cannot open encoder " + std::string(encoder->name) + ": " +
            AvError(result);
    Close();
    return false;
  }

  result = avcodec_parameters_from_context(stream->codecpar, codecCtx);
  if (result < 0) {
    error = "cannot copy encoder parameters: " + AvError(result);
    Close();
    return false;
  }

  stream->time_base = codecCtx->time_base;

  // The audio stream must exist before the container header is written.
  if (audio && !m_OpenAudio(*audio, error)) {
    Close();
    return false;
  }

  if (!(formatCtx->oformat->flags & AVFMT_NOFILE)) {
    result = avio_open(&formatCtx->pb, url.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) {
      error = "cannot open output \"" + url + "\": " + AvError(result);
      Close();
      return false;
    }
  }

  if (isNetwork)
    formatCtx->flags |= AVFMT_FLAG_FLUSH_PACKETS;

  result = avformat_write_header(formatCtx, nullptr);
  if (result < 0) {
    error = "cannot write container header: " + AvError(result);
    Close();
    return false;
  }
  m_headerWritten = true;

  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    error = "cannot allocate video frame";
    Close();
    return false;
  }
  frame->format = pixelFormat;
  frame->width = m_width;
  frame->height = m_height;

  result = av_frame_get_buffer(frame, 32);
  if (result < 0) {
    error = "cannot allocate video frame buffer: " + AvError(result);
    av_frame_free(&frame);
    Close();
    return false;
  }
  m_frame = frame;

  error.clear();
  return true;
}

// ---------------------------------------------------------------------------
// Audio stream
// ---------------------------------------------------------------------------

bool MediaEncoder::m_OpenAudio(
  const AudioStreamConfig &config,
  std::string &error
) {
  if (config.sampleRate <= 0 || config.channels <= 0) {
    error = "invalid audio stream format";
    return false;
  }

  const AVCodec *encoder = avcodec_find_encoder_by_name("aac");
  if (!encoder)
    encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!encoder) {
    error = "no AAC encoder is available in avcodec";
    return false;
  }

  AVFormatContext *formatCtx = static_cast<AVFormatContext *>(m_formatCtx);

  AVStream *stream = avformat_new_stream(formatCtx, nullptr);
  if (!stream) {
    error = "cannot create audio stream";
    return false;
  }
  m_audioStream = stream;

  AVCodecContext *codecCtx = avcodec_alloc_context3(encoder);
  if (!codecCtx) {
    error = "cannot allocate audio codec context";
    m_FreeAudio();
    return false;
  }
  m_audioCodecCtx = codecCtx;

  codecCtx->sample_rate = config.sampleRate;
  av_channel_layout_default(&codecCtx->ch_layout, config.channels);
  codecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
  codecCtx->time_base = AVRational{1, config.sampleRate};
  codecCtx->bit_rate =
    static_cast<int64_t>(std::max(16, config.bitrateKbps)) * 1000;

  if (formatCtx->oformat->flags & AVFMT_GLOBALHEADER)
    codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  int result = avcodec_open2(codecCtx, encoder, nullptr);
  if (result < 0) {
    error = "cannot open AAC encoder: " + AvError(result);
    m_FreeAudio();
    return false;
  }

  m_audioFrameSize = codecCtx->frame_size > 0 ? codecCtx->frame_size : 1024;

  result = avcodec_parameters_from_context(stream->codecpar, codecCtx);
  if (result < 0) {
    error = "cannot copy audio encoder parameters: " + AvError(result);
    m_FreeAudio();
    return false;
  }
  stream->time_base = codecCtx->time_base;

  // Convert the captured interleaved S16 PCM into whatever layout the AAC
  // encoder expects (the native encoder uses planar float).
  AVChannelLayout inputLayout;
  av_channel_layout_default(&inputLayout, config.channels);

  SwrContext *resampler = nullptr;
  result = swr_alloc_set_opts2(
    &resampler,
    &codecCtx->ch_layout,
    codecCtx->sample_fmt,
    codecCtx->sample_rate,
    &inputLayout,
    AV_SAMPLE_FMT_S16,
    config.sampleRate,
    0,
    nullptr);
  if (result < 0 || !resampler) {
    error = "cannot create audio resampler: " + AvError(result);
    m_FreeAudio();
    return false;
  }
  m_audioResampler = resampler;

  result = swr_init(resampler);
  if (result < 0) {
    error = "cannot initialize audio resampler: " + AvError(result);
    m_FreeAudio();
    return false;
  }

  AVAudioFifo *fifo = av_audio_fifo_alloc(
    codecCtx->sample_fmt,
    codecCtx->ch_layout.nb_channels,
    m_audioFrameSize * 4);
  if (!fifo) {
    error = "cannot allocate the audio FIFO";
    m_FreeAudio();
    return false;
  }
  m_audioFifo = fifo;

  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    error = "cannot allocate audio frame";
    m_FreeAudio();
    return false;
  }
  m_audioFrame = frame;

  frame->format = codecCtx->sample_fmt;
  frame->sample_rate = codecCtx->sample_rate;
  frame->nb_samples = m_audioFrameSize;

  result = av_channel_layout_copy(&frame->ch_layout, &codecCtx->ch_layout);
  if (result < 0) {
    error = "cannot copy the audio channel layout: " + AvError(result);
    m_FreeAudio();
    return false;
  }

  result = av_frame_get_buffer(frame, 0);
  if (result < 0) {
    error = "cannot allocate the audio frame buffer: " + AvError(result);
    m_FreeAudio();
    return false;
  }

  m_audioFifoBaseValid = false;
  m_audioFifoBasePts = 0;
  m_lastAudioPts = -1;
  return true;
}

bool MediaEncoder::EncodeAudio(
  const i16 *samples,
  i32 frames,
  i64 ptsMs,
  std::string &error
) {
  if (!m_audioCodecCtx || !m_audioResampler || !m_audioFifo || !m_audioFrame) {
    error = "audio stream is not open";
    return false;
  }

  if (!samples || frames <= 0)
    return true;

  AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_audioCodecCtx);
  SwrContext *resampler = static_cast<SwrContext *>(m_audioResampler);
  AVAudioFifo *fifo = static_cast<AVAudioFifo *>(m_audioFifo);

  i32 outputSamples = swr_get_out_samples(resampler, frames);
  if (outputSamples <= 0) {
    error = "cannot size the audio resampler output";
    return false;
  }

  i32 channels = codecCtx->ch_layout.nb_channels;
  i32 bytesPerSample =
    av_get_bytes_per_sample(codecCtx->sample_fmt);
  usize required = static_cast<usize>(outputSamples) *
                   static_cast<usize>(channels) *
                   static_cast<usize>(bytesPerSample);
  if (m_audioConvertBuffer.size() < required)
    m_audioConvertBuffer.resize(required);

  u08 *outputPlanes[AV_NUM_DATA_POINTERS] = {};
  int outputLineSize = 0;
  if (av_samples_fill_arrays(
        outputPlanes,
        &outputLineSize,
        m_audioConvertBuffer.data(),
        channels,
        outputSamples,
        codecCtx->sample_fmt,
        1) < 0) {
    error = "cannot map the audio conversion buffer";
    return false;
  }

  const u08 *inputPlanes[1] = {
    reinterpret_cast<const u08 *>(samples)
  };
  int inputLineSize[1] = {
    frames * channels * static_cast<i32>(sizeof(i16))
  };

  i32 converted = swr_convert(
    resampler,
    outputPlanes,
    outputSamples,
    inputPlanes,
    frames);
  if (converted < 0) {
    error = "cannot convert audio samples: " + AvError(converted);
    return false;
  }
  if (converted == 0)
    return true;

  // Keep the FIFO's read head tied to the capture timeline. Small drift is
  // left alone so consecutive buffers stay sample-continuous; a large jump
  // (device reset, stalled capture) re-anchors the timeline.
  i32 queuedBefore = av_audio_fifo_size(fifo);
  i64 chunkPts = av_rescale_q(
    std::max<i64>(0, ptsMs),
    AVRational{1, 1000},
    codecCtx->time_base);

  if (!m_audioFifoBaseValid) {
    m_audioFifoBasePts = chunkPts - queuedBefore;
    m_audioFifoBaseValid = true;
  } else {
    i64 expected = m_audioFifoBasePts + queuedBefore;
    i64 difference = chunkPts - expected;
    if (difference < 0)
      difference = -difference;

    if (difference > codecCtx->sample_rate / 5) // 200 ms
      m_audioFifoBasePts = chunkPts - queuedBefore;
  }

  int written = av_audio_fifo_write(
    fifo,
    reinterpret_cast<void **>(outputPlanes),
    converted);
  if (written < converted) {
    error = "cannot buffer converted audio samples";
    return false;
  }

  return m_DrainAudioFifo(error);
}

bool MediaEncoder::m_DrainAudioFifo(std::string &error) {
  AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_audioCodecCtx);
  AVAudioFifo *fifo = static_cast<AVAudioFifo *>(m_audioFifo);
  AVFrame *frame = static_cast<AVFrame *>(m_audioFrame);

  while (av_audio_fifo_size(fifo) >= m_audioFrameSize) {
    // The audio encoder may still reference the previous frame's buffer, so
    // obtain a private one before filling it from the FIFO.
    int result = av_frame_make_writable(frame);
    if (result < 0) {
      error = "cannot make audio frame writable: " + AvError(result);
      return false;
    }

    if (av_audio_fifo_read(
          fifo,
          reinterpret_cast<void **>(frame->data),
          m_audioFrameSize) < m_audioFrameSize) {
      error = "cannot read buffered audio samples";
      return false;
    }

    frame->nb_samples = m_audioFrameSize;

    i64 pts = m_audioFifoBasePts;
    m_audioFifoBasePts += m_audioFrameSize;
    if (pts < 0)
      pts = 0;
    if (m_lastAudioPts >= 0 && pts <= m_lastAudioPts)
      pts = m_lastAudioPts + 1;
    m_lastAudioPts = pts;
    frame->pts = pts;

    result = avcodec_send_frame(codecCtx, frame);
    if (result == AVERROR(EAGAIN)) {
      if (!m_WriteAudioPackets(error))
        return false;
      result = avcodec_send_frame(codecCtx, frame);
    }
    if (result < 0) {
      error = "cannot send audio frame to encoder: " + AvError(result);
      return false;
    }

    if (!m_WriteAudioPackets(error))
      return false;
  }

  return true;
}

bool MediaEncoder::m_WriteAudioPackets(std::string &error) {
  AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_audioCodecCtx);
  AVFormatContext *formatCtx = static_cast<AVFormatContext *>(m_formatCtx);
  AVStream *stream = static_cast<AVStream *>(m_audioStream);
  AVPacket *packet = av_packet_alloc();

  if (!packet) {
    error = "cannot allocate audio packet";
    return false;
  }

  while (true) {
    int result = avcodec_receive_packet(codecCtx, packet);

    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
      av_packet_free(&packet);
      return true;
    }

    if (result < 0) {
      error = "cannot receive encoded audio packet: " + AvError(result);
      av_packet_free(&packet);
      return false;
    }

    av_packet_rescale_ts(packet, codecCtx->time_base, stream->time_base);
    packet->stream_index = stream->index;

    result = av_interleaved_write_frame(formatCtx, packet);
    if (result < 0) {
      error = "cannot write encoded audio packet: " + AvError(result);
      av_packet_free(&packet);
      return false;
    }
  }
}

bool MediaEncoder::m_FlushAudio(std::string &error) {
  if (!m_audioCodecCtx || !m_headerWritten)
    return true;

  AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_audioCodecCtx);
  AVAudioFifo *fifo = static_cast<AVAudioFifo *>(m_audioFifo);

  // The encoder expects whole frames. Pad the tail with silence so the last
  // fraction of a frame is not lost and the stream ends on a frame boundary.
  i32 remaining = av_audio_fifo_size(fifo);
  if (remaining > 0) {
    i32 padding = m_audioFrameSize - (remaining % m_audioFrameSize);
    if (padding != m_audioFrameSize) {
      i32 channels = codecCtx->ch_layout.nb_channels;
      i32 bytesPerSample = av_get_bytes_per_sample(codecCtx->sample_fmt);
      std::vector<u08> zeros(
        static_cast<usize>(padding) *
          static_cast<usize>(channels) *
          static_cast<usize>(bytesPerSample),
        0);

      u08 *planes[AV_NUM_DATA_POINTERS] = {};
      int lineSize = 0;
      if (av_samples_fill_arrays(
            planes,
            &lineSize,
            zeros.data(),
            channels,
            padding,
            codecCtx->sample_fmt,
            1) < 0) {
        error = "cannot map the audio padding buffer";
        return false;
      }

      if (av_audio_fifo_write(
            fifo,
            reinterpret_cast<void **>(planes),
            padding) < padding) {
        error = "cannot pad the audio FIFO";
        return false;
      }
    }

    if (!m_DrainAudioFifo(error))
      return false;
  }

  int result = avcodec_send_frame(codecCtx, nullptr);
  if (result < 0 && result != AVERROR_EOF) {
    error = "cannot flush the audio encoder: " + AvError(result);
    return false;
  }

  return m_WriteAudioPackets(error);
}

// ---------------------------------------------------------------------------
// Video stream
// ---------------------------------------------------------------------------

bool MediaEncoder::m_PrepareScaler(
  i32 srcWidth,
  i32 srcHeight,
  SourcePixelFormat srcFormat,
  std::string &error
) {
  if (m_scaler && m_scalerWidth == srcWidth && m_scalerHeight == srcHeight &&
      m_scalerFormat == srcFormat)
    return true;

  AVPixelFormat avFormat = ToAvPixelFormat(srcFormat);
  if (avFormat == AV_PIX_FMT_NONE) {
    error = "unsupported source pixel format";
    return false;
  }

  sws_freeContext(static_cast<SwsContext *>(m_scaler));
  m_scaler = nullptr;

  m_scaler = sws_getContext(
    srcWidth,
    srcHeight,
    avFormat,
    m_width,
    m_height,
    static_cast<AVPixelFormat>(m_pixFmt),
    SWS_BILINEAR,
    nullptr,
    nullptr,
    nullptr);

  if (!m_scaler) {
    error = "cannot create swscale context";
    return false;
  }

  // Prefer BT.709 for the RGB -> YUV conversion; this is what desktop
  // recordings usually expect.
  int coefficients = SWS_CS_ITU709;
  sws_setColorspaceDetails(
    static_cast<SwsContext *>(m_scaler),
    sws_getCoefficients(coefficients),
    1, // source (RGB) is full range
    sws_getCoefficients(coefficients),
    0, // destination (YUV) is limited/MPEG range
    0,
    1 << 16,
    1 << 16);

  m_scalerWidth = srcWidth;
  m_scalerHeight = srcHeight;
  m_scalerFormat = srcFormat;
  return true;
}

bool MediaEncoder::m_WriteVideoPackets(std::string &error) {
  AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_codecCtx);
  AVFormatContext *formatCtx = static_cast<AVFormatContext *>(m_formatCtx);
  AVStream *stream = static_cast<AVStream *>(m_stream);
  AVPacket *packet = av_packet_alloc();

  if (!packet) {
    error = "cannot allocate packet";
    return false;
  }

  while (true) {
    int result = avcodec_receive_packet(codecCtx, packet);

    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
      av_packet_free(&packet);
      return true;
    }

    if (result < 0) {
      error = "cannot receive encoded packet: " + AvError(result);
      av_packet_free(&packet);
      return false;
    }

    av_packet_rescale_ts(packet, codecCtx->time_base, stream->time_base);
    packet->stream_index = stream->index;

    result = av_interleaved_write_frame(formatCtx, packet);
    if (result < 0) {
      error = "cannot write encoded packet: " + AvError(result);
      av_packet_free(&packet);
      return false;
    }
    // av_interleaved_write_frame() takes ownership of the packet's data and
    // resets it, so the same AVPacket can be reused.
  }
}

bool MediaEncoder::m_FlushVideo(std::string &error) {
  AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_codecCtx);

  if (!codecCtx || !m_headerWritten)
    return true;

  int result = avcodec_send_frame(codecCtx, nullptr);
  if (result < 0 && result != AVERROR_EOF) {
    error = "cannot flush encoder: " + AvError(result);
    return false;
  }

  return m_WriteVideoPackets(error);
}

bool MediaEncoder::EncodeVideo(
  const u08 *data,
  i32 srcWidth,
  i32 srcHeight,
  SourcePixelFormat srcFormat,
  i64 ptsMs,
  std::string &error
) {
  if (!m_codecCtx || !m_frame || !m_stream || !m_formatCtx) {
    error = "encoder is not open";
    return false;
  }

  if (!data || srcWidth <= 0 || srcHeight <= 0) {
    error = "invalid source frame";
    return false;
  }

  if (!m_PrepareScaler(srcWidth, srcHeight, srcFormat, error))
    return false;

  AVFrame *frame = static_cast<AVFrame *>(m_frame);
  AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_codecCtx);

  int result = av_frame_make_writable(frame);
  if (result < 0) {
    error = "cannot make frame writable: " + AvError(result);
    return false;
  }

  u32 bytesPerPixel = 0;
  switch (srcFormat) {
    case kSourcePixelFormat_RGBA8:
    case kSourcePixelFormat_BGRA8:
    case kSourcePixelFormat_RGB10A2:
    case kSourcePixelFormat_BGR10A2:
      bytesPerPixel = 4;
      break;
    case kSourcePixelFormat_RGBAF16:
      bytesPerPixel = 8;
      break;
    case kSourcePixelFormat_RGBAF32:
      bytesPerPixel = 16;
      break;
    default:
      error = "unsupported source pixel format";
      return false;
  }

  const u08 *sourceData[4] = {data, nullptr, nullptr, nullptr};
  int sourceLinesize[4] = {
    srcWidth * static_cast<int>(bytesPerPixel), 0, 0, 0
  };

  int scaled = sws_scale(
    static_cast<SwsContext *>(m_scaler),
    sourceData,
    sourceLinesize,
    0,
    srcHeight,
    frame->data,
    frame->linesize);
  if (scaled <= 0) {
    error = "cannot convert frame to YUV420P";
    return false;
  }

  // Feed milliseconds into the encoder's time base. libx264 normally keeps
  // 1/1000, but avcodec_open2() is allowed to adjust it.
  i64 pts = av_rescale_q(ptsMs, AVRational{1, 1000}, codecCtx->time_base);
  if (pts <= m_lastPts)
    pts = m_lastPts + 1;
  m_lastPts = pts;

  frame->pts = pts;
  frame->color_range = AVCOL_RANGE_MPEG;
  frame->colorspace = AVCOL_SPC_BT709;
  frame->color_primaries = AVCOL_PRI_BT709;
  frame->color_trc = AVCOL_TRC_BT709;

  result = avcodec_send_frame(codecCtx, frame);
  if (result < 0) {
    error = "cannot send frame to encoder: " + AvError(result);
    return false;
  }

  return m_WriteVideoPackets(error);
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void MediaEncoder::m_FreeAudio() {
  if (m_audioFrame) {
    AVFrame *frame = static_cast<AVFrame *>(m_audioFrame);
    av_frame_free(&frame);
    m_audioFrame = nullptr;
  }

  if (m_audioFifo) {
    AVAudioFifo *fifo = static_cast<AVAudioFifo *>(m_audioFifo);
    av_audio_fifo_free(fifo);
    m_audioFifo = nullptr;
  }

  if (m_audioResampler) {
    SwrContext *resampler = static_cast<SwrContext *>(m_audioResampler);
    swr_free(&resampler);
    m_audioResampler = nullptr;
  }

  if (m_audioCodecCtx) {
    AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_audioCodecCtx);
    avcodec_free_context(&codecCtx);
    m_audioCodecCtx = nullptr;
  }

  // The stream itself is owned by the format context.
  m_audioStream = nullptr;
  m_audioConvertBuffer.clear();
  m_audioFrameSize = 1024;
  m_audioFifoBaseValid = false;
  m_audioFifoBasePts = 0;
  m_lastAudioPts = -1;
}

void MediaEncoder::Close() {
  if (m_headerWritten) {
    std::string ignored;
    if (m_codecCtx)
      m_FlushVideo(ignored);
    if (m_audioCodecCtx)
      m_FlushAudio(ignored);

    if (m_formatCtx)
      av_write_trailer(static_cast<AVFormatContext *>(m_formatCtx));

    m_headerWritten = false;
  }

  m_FreeAudio();

  if (m_frame) {
    AVFrame *frame = static_cast<AVFrame *>(m_frame);
    av_frame_free(&frame);
    m_frame = nullptr;
  }

  if (m_scaler) {
    sws_freeContext(static_cast<SwsContext *>(m_scaler));
    m_scaler = nullptr;
  }

  if (m_codecCtx) {
    AVCodecContext *codecCtx = static_cast<AVCodecContext *>(m_codecCtx);
    avcodec_free_context(&codecCtx);
    m_codecCtx = nullptr;
  }

  if (m_formatCtx) {
    AVFormatContext *formatCtx = static_cast<AVFormatContext *>(m_formatCtx);

    if (formatCtx->pb && !(formatCtx->oformat->flags & AVFMT_NOFILE))
      avio_closep(&formatCtx->pb);

    avformat_free_context(formatCtx);
    m_formatCtx = nullptr;
  }

  m_stream = nullptr;
  m_headerWritten = false;
  m_scalerWidth = 0;
  m_scalerHeight = 0;
  m_scalerFormat = kSourcePixelFormat_Unknown;
  m_lastPts = -1;
}
