#include "record/lightflow.hpp"
#include "skylink/skyCompositor.hpp"
#include "skylink/skyRenderer.hpp"
#include "skylink/skySystemIO.hpp"

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

    if (mod)
      mod->OnCompositorPreRender(this);

    // Force movie mode only for the IsCapturingMovie() queries that happen
    // inside this Compositor::Render() call. Other game systems keep seeing the
    // original value, which keeps the normal screen rendering alive.
    bool forceMovie = mod && mod->ShouldForceMovie() && mod->IsMovieModeActive();

    g_forceMovieThisRender = forceMovie;
    METABINARY_FN(Compositor, Render, 0).Call(this, jobBarn, cameraSystem, systemUI);
    g_forceMovieThisRender = false;

    if (mod)
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
  (const RenderOutline *barn, RenderTarget *targets, cstring name)
) {
  RenderTarget *result = FindRenderTarget(barn, targets, name);

  if (barn && targets) {
    LightFlowMod *mod = GetLightFlowMod();
    if (mod)
      mod->OnRenderTargetsFound(barn, targets);
  }

  return result;
}
