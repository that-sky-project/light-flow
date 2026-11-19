#include <Windows.h>

#include <cstdio>
#include <string>

#include <htmodloader/htmod.h>

#include "utils/types.hpp"
#include "metabinary/assert.hpp"
#include "metabinary/binaryDescriptor.hpp"
#include "metabinary/metabinary.hpp"
#include "lightflow/lightFlow.hpp"
#include "skylink/skyCompositor.hpp"

HMODULE hModuleDll = nullptr;

namespace {

HTHandle hKeyMenu = HT_INVALID_HANDLE;
HTHandle hKeyCapture = HT_INVALID_HANDLE;



bool InitializeMetaBinary(HMODULE module) {
  std::wstring folder = GetModuleFolder(module);
  std::wstring path = folder + L"\\Data\\BinaryDescriptor.json";
  std::string json = ReadFileString(path);

  if (json.empty()) {
    HTTellText(
      "[light-flow] Cannot read Data\\BinaryDescriptor.json next to the mod.");
    return false;
  }

  static BinaryDescriptorBarn binaryDescriptorBarn;
  binaryDescriptorBarn.Initialize(json.c_str());

  static const char *kRequiredDescriptors[] = {
    "Compositor::Render$0",
    "GetRenderer$0",
    "FindRenderTarget$0",
    "Texture::GetReadableTexture$0",
    "SystemIO::IsCapturingMovie$0",
    "Renderer::GetPixels$0",
  };

  for (const char *name : kRequiredDescriptors) {
    const BinaryDescriptor *desc =
      binaryDescriptorBarn.GetDescriptorByName(name);
    if (!desc || !desc->IsValid()) {
      HTTellText(
        "[light-flow] Binary descriptor \"%s\" is missing or invalid.",
        name);
      return false;
    }
  }

  static MetaBinarySystem metaBinarySystem;
  metaBinarySystem.Initialize(&binaryDescriptorBarn);

  return true;
}

void __fastcall HookCompositorRender(
  Sky::Compositor *pThis,
  Sky::Scene *scene,
  Sky::Camera *camera,
  Sky::Ambience *ambience,
  Sky::Ocean *ocean,
  Sky::JobBarn *jobBarn,
  Sky::CameraSystem *cameraSystem,
  Sky::SystemUI *systemUI
) {
  LightFlow::LightFlowMod *mod = LightFlow::g_lightFlow;

  if (mod)
    mod->OnCompositorPreRender(pThis);

  u08 *base = reinterpret_cast<u08 *>(pThis);
  u32 frameIndex = base
    ? *reinterpret_cast<u32 *>(base + Sky::CompositorOffsets::kFrameCounter)
    : 0;
  bool screenshotPending = base &&
    *(base + Sky::CompositorOffsets::kScreenshotRequested) != 0;

  // Force movie mode only for the IsCapturingMovie() query that happens
  // inside this Compositor::Render() call. Other game systems keep seeing the
  // original value, which keeps the normal screen rendering alive.
  bool forceMovie =
    mod && mod->ShouldForceMovie() && mod->IsMovieModeActive();
  LightFlow::g_forceMovieThisRender = forceMovie;

  Sky::Compositor::sfn_Render_0.Call(
    pThis,
    scene,
    camera,
    ambience,
    ocean,
    jobBarn,
    cameraSystem,
    systemUI);

  LightFlow::g_forceMovieThisRender = false;

  // Re-check the global: the mod may have been unloaded while the original
  // render call was running.
  if (mod && LightFlow::g_lightFlow == mod)
    mod->OnCompositorRender(
      pThis,
      frameIndex,
      screenshotPending,
      forceMovie);
}

void __fastcall OnToggleMenu(HTKeyEvent *event) {
  if ((event->flags & HTKeyEventFlags_Mask) != HTKeyEventFlags_Down)
    return;
  if (LightFlow::g_lightFlow)
    LightFlow::g_lightFlow->ToggleMenu();
}

void __fastcall OnToggleCapture(HTKeyEvent *event) {
  if ((event->flags & HTKeyEventFlags_Mask) != HTKeyEventFlags_Down)
    return;
  if (LightFlow::g_lightFlow)
    LightFlow::g_lightFlow->RequestToggleCapture();
}

void RegisterHotkeys() {
  hKeyMenu = HTHotkeyRegisterEx(
    hModuleDll,
    "Toggle Light Flow panel",
    HTKey_F8,
    HTHotkeyFlags_NoBlock);
  hKeyCapture = HTHotkeyRegisterEx(
    hModuleDll,
    "Start/stop Light Flow recording",
    HTKey_F9,
    HTHotkeyFlags_NoBlock);

  HTHotkeyListen(hKeyMenu, OnToggleMenu);
  HTHotkeyListen(hKeyCapture, OnToggleCapture);
}

} // namespace

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnInit(void *) {
  SetAssertHandler([](cstring expr, cstring msg, cstring file, u32 line, u32) {
    HTTellText(
      "[light-flow][ASSERT] %s (%u): %s, %s",
      file,
      line,
      expr,
      msg);
  });

  // The detour must be set before MetaBinarySystem::Initialize() assigns the
  // address; Redirect() refuses to replace an address that is already valid.
  Sky::Compositor::sfn_Render_0.Function().Redirect(
    reinterpret_cast<BinaryFunctionBase::FuncPtr>(&HookCompositorRender));

  if (!InitializeMetaBinary(hModuleDll))
    return HT_FAIL;

  LightFlow::g_lightFlow = new LightFlow::LightFlowMod();
  if (!LightFlow::g_lightFlow->Initialize(hModuleDll)) {
    delete LightFlow::g_lightFlow;
    LightFlow::g_lightFlow = nullptr;
    return HT_FAIL;
  }

  RegisterHotkeys();

  Sky::Compositor::sfn_Render_0.Hook();

  HTTellText(
    "[light-flow] Ready. Compositor::Render: %p",
    (void *)Sky::Compositor::sfn_Render_0.Function().Address());

  return HT_SUCCESS;
}

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnEnable(void *) {
  HTTellText("[light-flow] Enabled. F8 opens the panel, F9 records.");
  return HT_SUCCESS;
}

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnDisable(void *) {
  if (LightFlow::g_lightFlow)
    LightFlow::g_lightFlow->OnDisable();
  return HT_SUCCESS;
}

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnDeinit(void *) {
  // Clear the global first so a render that is still in flight cannot touch
  // the object while it is being destroyed.
  LightFlow::LightFlowMod *mod = LightFlow::g_lightFlow;
  LightFlow::g_lightFlow = nullptr;

  if (mod) {
    mod->Shutdown();
    delete mod;
  }

  if (Sky::Compositor::sfn_Render_0.Function().IsHookEnabled())
    Sky::Compositor::sfn_Render_0.Unhook();

  HTHotkeyUnlisten(hKeyMenu, nullptr);
  HTHotkeyUnlisten(hKeyCapture, nullptr);

  return HT_SUCCESS;
}

extern "C" __declspec(dllexport) void HTMLAPI HTModRenderGui(
  float timeElapsed,
  void *
) {
  if (LightFlow::g_lightFlow)
    LightFlow::g_lightFlow->OnGui(timeElapsed);
}

BOOL APIENTRY DllMain(
  HMODULE hModule,
  DWORD reason,
  LPVOID
) {
  if (reason == DLL_PROCESS_ATTACH) {
    hModuleDll = hModule;
    DisableThreadLibraryCalls(hModule);
  }
  return TRUE;
}
