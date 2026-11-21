#include "utils/loaderUtils.hpp"
#include "record/modSettings.hpp"

// ---------------------------------------------------------------------------
// [SECTION] Settings
// ---------------------------------------------------------------------------

void Settings::Load() {
  HTModLoader::LoadSetting("output", &output);
  HTModLoader::LoadSetting("codec", &codec);
  HTModLoader::LoadSetting("bitrate_kbps", &bitrateKbps);
  HTModLoader::LoadSetting("target_fps", &targetFps);
  HTModLoader::LoadSetting("queue_size", &queueSize);
  HTModLoader::LoadSetting("show_overlay", &showOverlay);
  HTModLoader::LoadSetting("swap_red_blue", &swapRedBlue);
}

void Settings::Save() const {
  HTModLoader::SaveSetting("output", output);
  HTModLoader::SaveSetting("codec", codec);
  HTModLoader::SaveSetting("bitrate_kbps", bitrateKbps);
  HTModLoader::SaveSetting("target_fps", targetFps);
  HTModLoader::SaveSetting("queue_size", queueSize);
  HTModLoader::SaveSetting("show_overlay", showOverlay);
  HTModLoader::SaveSetting("swap_red_blue", swapRedBlue);
}
