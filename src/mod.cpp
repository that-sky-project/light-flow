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
#include "skylink/skyCompositor.hpp"
#include "skylink/skyRenderer.hpp"

// ----------------------------------------------------------------------------
// [SECTION] Globals
// ----------------------------------------------------------------------------

static Logger *g_logger = nullptr;
static LightFlowMod *g_lightFlowInstance = nullptr;
static HMODULE g_module = nullptr;
static bool g_metaBinaryReady = false;

namespace {

// Map the HTModLoader game edition onto the descriptor profile name. Both the
// MetaBinary descriptor list (BinaryDescriptor.json) and the structure layouts
// in skylink use the same "netease"/"tgc" spelling.
bool ResolveGameProfile(
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

// Fetch ImGui's context and allocator from the loader and bind them to this
// module's own ImGui build. Must run every frame: HTModRenderGui() may be
// invoked on a different thread than the one that ran the previous frame.
bool BindImGuiContext() {
  HTImGuiContextsEx ctxex;
  if (HTImGuiDispatchEx(&ctxex) != HT_SUCCESS || !ctxex.ctx.pImGui)
    return false;

  ImGui::SetCurrentContext((ImGuiContext *)ctxex.ctx.pImGui);

  static bool allocatorsBound = false;
  if (!allocatorsBound) {
    allocatorsBound = true;
    ImGui::SetAllocatorFunctions(
      (ImGuiMemAllocFunc)ctxex.ctx.pImAllocatorAllocFunc,
      (ImGuiMemFreeFunc)ctxex.ctx.pImAllocatorFreeFunc,
      ctxex.ctx.pImAllocatorUserData);
  }
  return true;
}

// Report a fatal initialization problem to both the log file and the in-game
// console, then leave the game running with the mod's features disabled.
void ReportFatal(cstring message) {
  LOGE("%s", message);
  HTTellText("§5[Light Flow] %s", message);
}

} // namespace

// ----------------------------------------------------------------------------
// [SECTION] HTModLoader entry points
// ----------------------------------------------------------------------------

__declspec(dllexport) HTStatus HTMLAPI HTModOnInit(void *) {
  g_module = HTModLoader::GetCurrentModuleHandle();
  if (!g_module)
    g_module = HTGetModuleHandle(nullptr);

  TgcWString folder = Win64SysCall::GetModuleFolder(g_module);

  g_logger = new Logger();
  g_logger->Initialize(folder + L"\\light-flow.log");

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
  if (!ResolveGameProfile(status.edition, &profile)) {
    char message[256];
    snprintf(
      message,
      sizeof(message),
      "Unsupported game edition 0x%X, this mod supports NetEase (0x%X) and "
      "TGC (0x%X) only.",
      status.edition,
      HT_ImplSky_EditionChinese,
      HT_ImplSky_EditionInternation);
    ReportFatal(message);
    return HT_FAIL;
  }

  LOG("Game edition: %s", GetGameProfileName(profile));

  // 2. Load the descriptor list for this edition. BinaryDescriptorBarn picks
  //    root[profile][arch], so one JSON keeps both builds side by side.
  TgcWString path = folder + L"\\Data\\Manifests\\BinaryDescriptor.json";
  TgcString json = FsUtils::ReadFileUtf8(path);

  if (json.empty()) {
    ReportFatal("Cannot read Data\\Manifests\\BinaryDescriptor.json");
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
    ReportFatal("MetaBinarySystem is incomplete, the mod has been shut down.");
    return HT_FAIL;
  }
  g_metaBinaryReady = true;
  LOG("MetaBinarySystem initialized");

  // 4. Bring up the recorder and hook Compositor::Render(). The hook passes
  //    through to the original function at all times; capture is controlled by
  //    the F8/F9 hotkeys and by g_forceMovieThisRender.
  g_lightFlowInstance = new LightFlowMod();
  g_lightFlow = g_lightFlowInstance;

  if (!g_lightFlowInstance->Initialize(g_module, profile)) {
    ReportFatal("LightFlowMod initialization failed.");
    g_lightFlowInstance->Shutdown();
    return HT_FAIL;
  }

  // Learn the compositor's render target table before anything needs it. A
  // failure here means rtScreenshot can never be located, so disable the mod.
  auto &findRenderTarget = METABINARY_FN(, FindRenderTarget, 0);
  if (!findRenderTarget.Function().IsValid()) {
    ReportFatal("FindRenderTarget is not linked, the mod has been shut down.");
    g_lightFlowInstance->Shutdown();
    return HT_FAIL;
  }
  if (!findRenderTarget.Hook()) {
    ReportFatal("Cannot hook FindRenderTarget, the mod has been shut down.");
    g_lightFlowInstance->Shutdown();
    return HT_FAIL;
  }
  LOG(
    "Hooked FindRenderTarget at %p",
    reinterpret_cast<void *>(findRenderTarget.Function().Address()));

  auto &render = METABINARY_FN(Compositor, Render, 0);
  if (!render.Function().IsValid()) {
    ReportFatal("Compositor::Render is not linked, the mod has been shut down.");
    g_lightFlowInstance->Shutdown();
    return HT_FAIL;
  }

  if (!render.Hook()) {
    ReportFatal("Cannot hook Compositor::Render, the mod has been shut down.");
    g_lightFlowInstance->Shutdown();
    return HT_FAIL;
  }
  LOG(
    "Hooked Compositor::Render at %p",
    reinterpret_cast<void *>(render.Function().Address()));

  return HT_SUCCESS;
}

__declspec(dllexport) void HTMLAPI HTModRenderGui(float timeElapsed, void *) {
  (void)timeElapsed;

  if (!g_metaBinaryReady || !g_lightFlowInstance)
    return;

  if (!BindImGuiContext())
    return;

  g_lightFlowInstance->BuildScene();
}

__declspec(dllexport) HTStatus HTMLAPI HTModOnEnable(void *) {
  return HT_SUCCESS;
}

__declspec(dllexport) HTStatus HTMLAPI HTModOnDisable(void *) {
  if (g_lightFlowInstance)
    g_lightFlowInstance->OnDisable();
  return HT_SUCCESS;
}

__declspec(dllexport) HTStatus HTMLAPI HTModOnDeinit(void *) {
  // The loader does not unload mods in practice and the game may still call
  // the Compositor detour while shutting down, so stop the recording thread
  // and leave the object itself alive.
  if (g_lightFlowInstance)
    g_lightFlowInstance->Shutdown();

  if (g_logger)
    g_logger->Terminate();

  return HT_SUCCESS;
}
