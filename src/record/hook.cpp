#include "record/lightflow.hpp"
#include "skylink/skyCompositor.hpp"
#include "skylink/skyRenderer.hpp"
#include "skylink/skySystemIO.hpp"
#include "record/hook.hpp"

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
      if (!Hook::LinkCompositor(this, mod->Profile()) || !Hook::LinkRenderer())
        LOGE("Failed to install hooks of SystemIO");
      else
        s_hooked = true;
      s_first = false;
    }

    if (!s_hooked)
      METABINARY_FN(Compositor, Render, 0).Call(this, jobBarn, cameraSystem, systemUI);

    mod->OnCompositorPreRender(this);

    // Force movie mode only for the IsCapturingMovie() queries that happen
    // inside this Compositor::Render() call. Other game systems keep seeing the
    // original value, which keeps the normal screen rendering alive.
    bool forceMovie = mod && mod->ShouldForceMovie() && mod->IsMovieModeActive();

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

namespace Hook {

bool BaseHooks() {
// Learn the compositor's render target table before anything needs it. A
  // failure here means rtScreenshot can never be located, so disable the mod.
  auto &findRenderTarget = METABINARY_FN(, FindRenderTarget, 0);
  if (!findRenderTarget.Function().IsValid()) {
    LOGE("FindRenderTarget is not linked.");
    return false;
  }
  if (!findRenderTarget.Hook()) {
    LOGE("Cannot hook FindRenderTarget.");
    return false;
  }
  LOG("Hooked FindRenderTarget at %p",
    reinterpret_cast<void *>(findRenderTarget.Function().Address()));

  auto &render = METABINARY_FN(Compositor, Render, 0);
  if (!render.Function().IsValid()) {
    LOGE("Compositor::Render is not linked.");
    return false;
  }

  if (!render.Hook()) {
    LOGE("Cannot hook Compositor::Render.");
    return false;
  }
  LOG("Hooked Compositor::Render at %p",
    reinterpret_cast<void *>(render.Function().Address()));

  return true;
}

bool LinkCompositor(
  Compositor *compositor,
  GameProfile profile
) {
  SystemIO *systemIO = compositor->*Compositor::systemIO;
  if (!systemIO) {
    LOGE("SystemIO is unavailable.");
    return false;
  }

  auto &vf0 = METABINARY_FN(SystemIO, IsCapturingMovie, 0);
  if (!vf0.Function().IsValid())
    // Try to cache vftable first.
    vf0.Cache(systemIO);
  if (!vf0.Function().IsValid()) {
    // Report error if still failed.
    LOGE("SystemIO::IsCapturingMovie could not be resolved.");
    return false;
  }
  if (!vf0.Function().IsHookEnabled() && !vf0.Hook()) {
    LOGE("SystemIO::IsCapturingMovie could not be hooked.");
    return false;
  }

  // The NetEase client ORs two capture flags inside Compositor::Render().
  // The TGC client only consults one.
  if (profile == GameProfile::kNetEase) {
    auto &vf1 = METABINARY_FN(SystemIO, IsCapturingMovieCC, 0);
    if (!vf1.Function().IsValid())
      vf1.Cache(systemIO);
    // Both slots may resolve to the same implementation in some builds;
    // hooking the same address twice would chain two detours.
    if (
      vf1.Function().IsValid()
      && vf1.Function().Address() != vf0.Function().Address()
      && !vf1.Function().IsHookEnabled()
    ) {
      if (!vf1.Hook()) {
        LOGE("SystemIO::IsCapturingMovieCC could not be hooked.");
        return false;
      }
    }
  }

  LOG(
    "Hooked SystemIO::IsCapturingMovie[0] at %p",
    reinterpret_cast<void *>(vf0.Function().Address())
  );
  return true;
}

void UnlinkCompositor() {
  auto &vf0 = METABINARY_FN(SystemIO, IsCapturingMovie, 0);
  if (vf0.Function().IsHookEnabled())
    vf0.Unhook();

  auto &vf1 = METABINARY_FN(SystemIO, IsCapturingMovieCC, 0);
  if (vf1.Function().IsHookEnabled())
    vf1.Unhook();
}

bool LinkRenderer() {
  Renderer *renderer = GetRenderer();
  auto &vf = METABINARY_FN(Renderer, GetPixels, 0);
  static Renderer *s_cachedRenderer = nullptr;
  if (!s_cachedRenderer) {
    vf.Cache(renderer);
    if (!vf.Function().IsValid()) {
      LOGE("Renderer::GetPixels could not be resolved");
      return false;
    }
    s_cachedRenderer = renderer;
  } else if (s_cachedRenderer != renderer) {
    LOGE("renderer object changed");
    return false;
  }
}

}
