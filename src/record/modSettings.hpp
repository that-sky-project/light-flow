#ifndef __RECORD_MODSETTINGS_HPP__
#define __RECORD_MODSETTINGS_HPP__

#include "utils/types.hpp"

// ----------------------------------------------------------------------------
// [SECTION] Settings
// ----------------------------------------------------------------------------

struct Settings {
  // Empty means "auto": <mod folder>\captures\light-flow-<timestamp>.mp4.
  // A value containing "://" is used as a network URL (rtmp://, srt://, ...).
  TgcString output;
  TgcString codec = "libx264";
  i32 bitrateKbps = 12000;
  i32 targetFps = 30;
  i32 queueSize = 4;
  bool showOverlay = true;
  // Exchange the red and blue channels of every captured frame before it is
  // handed to the encoder.
  bool swapRedBlue = false;
  // Record the audio rendered by this game process (WASAPI process loopback)
  // into an AAC stream next to the video.
  bool audioEnabled = true;
  i32 audioBitrateKbps = 128;

  void Load();
  void Save() const;
};

#endif
