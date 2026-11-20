#ifndef __ENCODE_VIDEOENCODER_HPP__
#define __ENCODE_VIDEOENCODER_HPP__

#include <string>

#include "utils/types.hpp"
#include "skylink/skyGfx.hpp"
#include "record/sourcePixelFormat.hpp"

// Thin FFmpeg wrapper that turns consecutive RGBA/BGRA frames into an H.264
// (or MPEG-4) video file or network stream.
//
// The encoder is designed to be owned by a single worker thread. FFmpeg
// headers are kept out of this header on purpose.
class VideoEncoder {
public:
  VideoEncoder() = default;
  ~VideoEncoder();

  VideoEncoder(const VideoEncoder &) = delete;
  VideoEncoder &operator=(const VideoEncoder &) = delete;

  // Process-wide FFmpeg initialization. Safe to call repeatedly.
  static void GlobalInitialize();
  static void GlobalShutdown();

  // Open `url` with the requested codec. `width` and `height` define the
  // encoded resolution and must stay constant for the lifetime of the stream;
  // source frames of a different size are scaled by sws_scale().
  bool Open(
    const std::string &url,
    i32 width,
    i32 height,
    i32 fps,
    i32 bitrateKbps,
    const std::string &codec,
    std::string &error
  );

  bool Encode(
    const u08 *data,
    i32 srcWidth,
    i32 srcHeight,
    SourcePixelFormat srcFormat,
    i64 ptsMs,
    std::string &error
  );

  void Close();

  inline bool IsOpen() const { return m_codecCtx != nullptr; }
  inline i32 Width() const { return m_width; }
  inline i32 Height() const { return m_height; }
  inline const std::string &Url() const { return m_url; }

private:
  bool m_PrepareScaler(
    i32 srcWidth,
    i32 srcHeight,
    SourcePixelFormat srcFormat,
    std::string &error
  );
  bool m_WritePackets(std::string &error);
  bool m_Flush(std::string &error);

  void *m_formatCtx = nullptr;
  void *m_codecCtx = nullptr;
  void *m_stream = nullptr;
  void *m_scaler = nullptr;
  void *m_frame = nullptr;

  i32 m_width = 0;
  i32 m_height = 0;
  i32 m_fps = 30;
  i32 m_pixFmt = 0;
  i64 m_lastPts = -1;
  bool m_headerWritten = false;
  i32 m_scalerWidth = 0;
  i32 m_scalerHeight = 0;
  SourcePixelFormat m_scalerFormat = kSourcePixelFormat_Unknown;
  std::string m_url;
  std::string m_codecName;
};

#endif
