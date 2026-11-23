#ifndef __RECORD_HOOK_HPP__
#define __RECORD_HOOK_HPP__

#include "skylink/skySystemIO.hpp"
#include "record/lightflow.hpp"

// ----------------------------------------------------------------------------
// [SECTION] Hook Installer
// ----------------------------------------------------------------------------

namespace Hook {

bool BaseHooks();

bool LinkRenderer();

bool LinkSystemIO(
  SystemIO *compositor,
  GameProfile profile);

} // namespace Hook

#endif
