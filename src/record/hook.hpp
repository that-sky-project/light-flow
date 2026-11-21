#ifndef __RECORD_HOOK_HPP__
#define __RECORD_HOOK_HPP__

#include "skylink/skyCompositor.hpp"
#include "record/lightflow.hpp"

// ----------------------------------------------------------------------------
// [SECTION] Hook Installer
// ----------------------------------------------------------------------------

namespace Hook {

bool BaseHooks();

bool LinkRenderer();

bool LinkCompositor(
  Compositor *compositor,
  GameProfile profile);

void UnlinkCompositor();

} // namespace Hook

#endif
