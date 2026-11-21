#include <cstdio>

#include <htmodloader/htmod.h>
#include <htmodloader/htmodloader.h>
#include <htmodloader/backends/html_impl_sky.h>
#include <imgui.h>

#include "utils/types.hpp"
#include "utils/assert.hpp"
#include "utils/logger.hpp"
#include "utils/fsUtils.hpp"
#include "utils/utf8Utils.hpp"
#include "utils/winapicall.hpp"
#include "metabinary/binaryDescriptor.hpp"
#include "metabinary/metabinary.hpp"
#include "record/lightflow.hpp"
#include "record/hook.hpp"
#include "skylink/skyCompositor.hpp"
#include "skylink/skyRenderer.hpp"

// ----------------------------------------------------------------------------
// [SECTION] Globals
// ----------------------------------------------------------------------------

static bool g_Ready = false;

// Map the HTModLoader game edition onto the descriptor profile name. Both the
// MetaBinary descriptor list (BinaryDescriptor.json) and the structure layouts
// in skylink use the same "netease"/"tgc" spelling.
static bool s_ResolveGameProfile(
  HTGameEdition edition,
  GameProfile *out
) {
  if (edition & HT_ImplSky_EditionChinese) {
    *out = GameProfile::kNetEase;
    return true;
  }
  if (edition & HT_ImplSky_EditionInternation) {
    *out = GameProfile::kTgc;
    return true;
  }
  return false;
}

// ----------------------------------------------------------------------------
// [SECTION] HTModLoader entry points
// ----------------------------------------------------------------------------

__declspec(dllexport) HTStatus HTMLAPI HTModOnInit(void *) {
  HMODULE hModuleDll = HTModLoader::GetCurrentModuleHandle();
  TgcWString folder = Win64SysCall::GetModuleFolder(hModuleDll);

  // Logger will be cached by itself.
  (new Logger())->Initialize(folder + L"\\light-flow.log");

  LOG("------------------------------------------------------------");
  LOG("<-- Light Flow -->");
  LOG("  Copyright (c) 2026 That Sky Project, HTMonkeyG");
  LOG("  High quality in-game recording mod for Sky: Children of the Light");
  LOG("  https://www.github.com/that-sky-project/light-flow");
  LOG("------------------------------------------------------------");

  SetAssertHandler([](cstring expr, cstring msg, cstring file, u32 line, u32) {
    LOGW("Assertion failed at %s (%u): %s (%s)", file, line, expr, msg);
  });

  // 1. Ask the loader which edition is running. The same build of this mod
  //    supports both Chinese (NetEase) and international (TGC) clients.
  HTGameStatus status = {};
  HTGetGameStatus(&status);

  GameProfile profile = GameProfile::kTgc;
  if (!s_ResolveGameProfile(status.edition, &profile)) {
    char message[256];
    snprintf(
      message,
      sizeof(message),
      "Unsupported game edition 0x%X, this mod supports NetEase (0x%X) and "
      "TGC (0x%X) only.",
      status.edition,
      HT_ImplSky_EditionChinese,
      HT_ImplSky_EditionInternation);
    LOGE(message);
    return HT_FAIL;
  }

  LOG("Game edition: %s", GetGameProfileName(profile));

  // 2. Load the descriptor list for this edition. BinaryDescriptorBarn picks
  //    root[profile][arch], so one JSON keeps both builds side by side.
  TgcWString path = folder + L"\\Data\\Manifests\\BinaryDescriptor.json";
  TgcString json = FsUtils::ReadFileUtf8(path);

  if (json.empty()) {
    LOGE("Cannot read Data\\Manifests\\BinaryDescriptor.json");
    return HT_FAIL;
  }

  auto *binaryDescriptorBarn = new BinaryDescriptorBarn();
  binaryDescriptorBarn->Initialize(json.c_str(), GetGameProfileName(profile));
  LOG("Loaded BinaryDescriptor.json from disk - %llu bytes", json.length());

  // 3. Resolve every METABINARY-declared link. A missing descriptor disables
  //    the mod instead of leaving a half-linked function that could crash the
  //    game later.
  auto *metaBinarySystem = new MetaBinarySystem();
  metaBinarySystem->Initialize(binaryDescriptorBarn);
  if (!metaBinarySystem->IsValid()) {
    LOGE("MetaBinarySystem is incomplete.");
    return HT_FAIL;
  }
  LOG("MetaBinarySystem initialized");

  // 4. Bring up the recorder and hook Compositor::Render(). The hook passes
  //    through to the original function at all times; capture is controlled by
  //    the F8/F9 hotkeys and by g_forceMovieThisRender.
  auto *lightFlow = new LightFlowMod();

  if (!lightFlow->Initialize(hModuleDll, profile)) {
    LOGE("LightFlowMod initialization failed.");
    lightFlow->Shutdown();
    return HT_FAIL;
  }

  if (!Hook::BaseHooks())
    return HT_FAIL;

  g_Ready = true;
  return HT_SUCCESS;
}

__declspec(dllexport) void HTMLAPI HTModRenderGui(float timeElapsed, void *) {
  (void)timeElapsed;

  if (!g_Ready)
    return;

  static bool s_first = true;
  if (s_first) {
    s_first = false;

    HTImGuiContextsEx ctxex;
    HTImGuiDispatchEx(&ctxex);

    ImGui::SetCurrentContext((ImGuiContext *)ctxex.ctx.pImGui);
    ImGui::SetAllocatorFunctions(
      (ImGuiMemAllocFunc)ctxex.ctx.pImAllocatorAllocFunc,
      (ImGuiMemFreeFunc)ctxex.ctx.pImAllocatorFreeFunc,
      ctxex.ctx.pImAllocatorUserData);
  }

  GetLightFlowMod()->BuildScene();
}

__declspec(dllexport) HTStatus HTMLAPI HTModOnEnable(void *) {
  return HT_SUCCESS;
}
