#ifndef __RECORD_MEDIAENCODER_HPP__
#define __RECORD_MEDIAENCODER_HPP__

#include <string>
#include <vector>

#include "utils/types.hpp"
#include "skylink/skyGfx.hpp"
#include "record/video/sourcePixelFormat.hpp"

// Thin FFmpeg wrapper that muxes the captured render-target frames and,
// optionally, the PCM audio captured by AudioCapturer into one H.264 (or
// MPEG-4) + AAC file or network stream.
//
// The encoder is owned by the encoder worker thread; all methods must be
// called from that thread. FFmpeg headers are kept out of this header on
// purpose.
class MediaEncoder {
public:
  struct AudioStreamConfig {
    i32 sampleRate = 44100;
    i32 channels = 2;
    i32 bitrateKbps = 128;
  };

  MediaEncoder() = default;
  ~MediaEncoder();

  MediaEncoder(const MediaEncoder &) = delete;
  MediaEncoder &operator=(const MediaEncoder &) = delete;

  // Process-wide FFmpeg initialization. Safe to call repeatedly.
  static void GlobalInitialize();
  static void GlobalShutdown();

  // Open `url` with the requested codec. `width` and `height` define the
  // encoded resolution and must stay constant for the lifetime of the stream;
  // source frames of a different size are scaled by sws_scale(). When `audio`
  // is non-null an AAC stream in the given format is added to the container.
  bool Open(
    const std::string &url,
    i32 width,
    i32 height,
    i32 fps,
    i32 bitrateKbps,
    const std::string &codec,
    const AudioStreamConfig *audio,
    std::string &error
  );

  bool EncodeVideo(
    const u08 *data,
    i32 srcWidth,
    i32 srcHeight,
    SourcePixelFormat srcFormat,
    i64 ptsMs,
    std::string &error
  );

  // `samples` is interleaved 16-bit PCM in the format passed to Open().
  // `ptsMs` uses the same clock as EncodeVideo().
  bool EncodeAudio(
    const i16 *samples,
    i32 frames,
    i64 ptsMs,
    std::string &error
  );

  void Close();

  inline bool IsOpen() const { return m_formatCtx != nullptr; }
  inline bool HasAudio() const { return m_audioCodecCtx != nullptr; }
  inline i32 Width() const { return m_width; }
  inline i32 Height() const { return m_height; }
  inline const std::string &Url() const { return m_url; }

private:
  bool m_OpenAudio(const AudioStreamConfig &config, std::string &error);
  bool m_PrepareScaler(
    i32 srcWidth,
    i32 srcHeight,
    SourcePixelFormat srcFormat,
    std::string &error
  );
  bool m_DrainAudioFifo(std::string &error);

  bool m_WriteVideoPackets(std::string &error);
  bool m_WriteAudioPackets(std::string &error);
  bool m_FlushVideo(std::string &error);
  bool m_FlushAudio(std::string &error);

  void m_FreeAudio();

  void *m_formatCtx = nullptr;

  // Video.
  void *m_codecCtx = nullptr;
  void *m_stream = nullptr;
  void *m_scaler = nullptr;
  void *m_frame = nullptr;
  i32 m_width = 0;
  i32 m_height = 0;
  i32 m_fps = 30;
  i32 m_pixFmt = 0;
  i64 m_lastPts = -1;
  i32 m_scalerWidth = 0;
  i32 m_scalerHeight = 0;
  SourcePixelFormat m_scalerFormat = kSourcePixelFormat_Unknown;

  // Audio.
  void *m_audioCodecCtx = nullptr;
  void *m_audioStream = nullptr;
  void *m_audioResampler = nullptr;
  void *m_audioFifo = nullptr;
  void *m_audioFrame = nullptr;
  std::vector<u08> m_audioConvertBuffer;
  i32 m_audioFrameSize = 1024;
  i64 m_audioFifoBasePts = 0;
  bool m_audioFifoBaseValid = false;
  i64 m_lastAudioPts = -1;

  bool m_headerWritten = false;
  std::string m_url;
  std::string m_codecName;
};

#endif
