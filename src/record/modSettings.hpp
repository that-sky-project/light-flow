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
  bool forceMovieMode = true;
  bool showOverlay = true;

  void Load();
  void Save() const;
};

#endif
