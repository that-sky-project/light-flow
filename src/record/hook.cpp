#include "record/lightflow.hpp"
#include "skylink/skyCompositor.hpp"
#include "skylink/skyRenderer.hpp"
#include "skylink/skySystemIO.hpp"
#include "record/hook.hpp"

// Hooks referenced these variable.
static bool g_forceMovieThisRender = false;

// ----------------------------------------------------------------------------
// [SECTION] Detours
// ----------------------------------------------------------------------------

class HOOK_Compositor: public Compositor {
public:
  void Render(
    JobBarn *jobBarn,
    CameraSystem *cameraSystem,
    SystemUI *systemUI
  ) {
    LightFlowMod *mod = GetLightFlowMod();

    // Install hooks.
    static bool s_first = true;
    static bool s_hooked = false;
    if (s_first) {
      if (!Hook::LinkSystemIO(this->*Compositor::systemIO, mod->Profile()) || !Hook::LinkRenderer()) {
        LOGE("Failed to link SystemIO or Renderer");
        mod->Shutdown();
      } else
        s_hooked = true;
      s_first = false;
    }

    if (!s_hooked) {
      METABINARY_FN(Compositor, Render, 0).Call(this, jobBarn, cameraSystem, systemUI);
      return;
    }

    mod->OnCompositorPreRender(this);

    // Force movie mode only for the IsCapturingMovie() queries that happen
    // inside this Compositor::Render() call. Other game systems keep seeing the
    // original value, which keeps the normal screen rendering alive.
    bool forceMovie = mod && mod->ShouldForceMovie();

    g_forceMovieThisRender = forceMovie;
    METABINARY_FN(Compositor, Render, 0).Call(this, jobBarn, cameraSystem, systemUI);
    g_forceMovieThisRender = false;

    mod->OnCompositorRender(this, forceMovie);
  }
};

class HOOK_SystemIO: public SystemIO {
public:
  bool IsCapturingMovie() {
    if (g_forceMovieThisRender)
      return true;
    return METABINARY_FN(SystemIO, IsCapturingMovie, 0).Call(this);
  }

  bool IsCapturingMovieCC() {
    if (g_forceMovieThisRender)
      return true;
    return METABINARY_FN(SystemIO, IsCapturingMovieCC, 0).Call(this);
  }

  bool IsLoggedIn() {
    // There's only one xref of this function in Compositor::Render(), so the
    // side effect wont be significant.
    if (g_forceMovieThisRender)
      return false;
    return METABINARY_FN(SystemIO, IsLoggedIn, 0).Call(this);
  }
};

// ----------------------------------------------------------------------------
// [SECTION] Detour Declarations
// ----------------------------------------------------------------------------

METABINARY_HOOK_MFN(
  Compositor, Render, 0,
  &HOOK_Compositor::Render
)
METABINARY_HOOK_MFN(
  SystemIO, IsCapturingMovie, 0,
  &HOOK_SystemIO::IsCapturingMovie
)
METABINARY_HOOK_MFN(
  SystemIO, IsCapturingMovieCC, 0,
  &HOOK_SystemIO::IsCapturingMovieCC
)
METABINARY_HOOK_MFN(
  SystemIO, IsLoggedIn, 0,
  &HOOK_SystemIO::IsLoggedIn
)

// FindRenderTarget() is called by the compositor with the render target table
// itself. Capture that table on its first execution (and refresh it on later
// calls, in case the table is rebuilt after a resize), so FrameGrabber does
// not have to know where the compositor keeps it.
METABINARY_HOOK_FN(
  RenderTarget *, FindRenderTarget, 0,
  (const RenderOutline &renderOutline, const RenderTarget *renderTargets, cstring name)
) {
  LightFlowMod *mod = GetLightFlowMod();
  if (mod)
    mod->OnRenderTargetsFound(&renderOutline, renderTargets);

  return FindRenderTarget(renderOutline, renderTargets, name);
}

// ----------------------------------------------------------------------------
// [SECTION] Hook Installer
// ----------------------------------------------------------------------------

static bool s_TryHook(
  MetaBinaryFunction &mbf
) {
  if (!mbf.IsInitialized() || !mbf.Function().IsValid()) {
    LOGE("[%s] is invalid.", mbf.GetName());
    return false;
  }

  if (!mbf.Function().IsHookEnabled() && !mbf.Hook()) {
    LOGE("[%s] could not be hooked.", mbf.GetName());
    return false;
  }

  LOGI("Hooked [%s] @ %p", mbf.GetName(), (void *)mbf.Function().Address());

  return true;
}

template<typename C>
static bool s_TryHook(
  MetaBinaryVirtualFunction &mbvf,
  const C *obj
) {
  if (!mbvf.IsInitialized()) {
    LOGE("[%s] is invalid.", mbvf.GetName());
    return false;
  }

  if (!mbvf.Cache(obj)) {
    LOGE("[%s] could not be resolved.", mbvf.GetName());
    return false;
  }

  if (!mbvf.Function().IsHookEnabled() && !mbvf.Hook()) {
    LOGE("[%s] could not be hooked.", mbvf.GetName());
    return false;
  }

  LOGI("Hooked [%s] @ %p", mbvf.GetName(), (void *)mbvf.Function().Address());

  return true;
}

namespace Hook {

bool BaseHooks() {
  // Learn the compositor's render target table before anything needs it. A
  // failure here means rtScreenshot can never be located, so disable the mod.
  if (!s_TryHook(METABINARY_FN(, FindRenderTarget, 0)))
    return false;
  if (!s_TryHook(METABINARY_FN(Compositor, Render, 0)))
    return false;
  return true;
}

bool LinkSystemIO(
  SystemIO *systemIO,
  GameProfile profile
) {
  if (!systemIO) {
    LOGE("SystemIO is unavailable.");
    return false;
  }

  if (!s_TryHook(METABINARY_FN(SystemIO, IsCapturingMovie, 0), systemIO))
    return false;

  // The NetEase client needs much more functions....
  if (profile != GameProfile::kNetEase)
    return true;

  if (!s_TryHook(METABINARY_FN(SystemIO, IsCapturingMovieCC, 0), systemIO))
    return false;

  if (!s_TryHook(METABINARY_FN(SystemIO, IsLoggedIn, 0), systemIO))
    return false;

  return true;
}

bool LinkRenderer() {
  Renderer *renderer = GetRenderer();
  auto &vf = METABINARY_FN(Renderer, GetPixels, 0);
  if (!vf.Function().IsValid()) {
    vf.Cache(renderer);
    if (!vf.Function().IsValid()) {
      LOGE("Renderer::GetPixels could not be resolved");
      return false;
    }
  }

  return true;
}

}
