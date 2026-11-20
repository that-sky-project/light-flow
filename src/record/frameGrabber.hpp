#ifndef __RECORD_FRAMEGRABBER_HPP__
#define __RECORD_FRAMEGRABBER_HPP__

#include <string>
#include <vector>

#include "utils/types.hpp"
#include "skylink/skyCompositor.hpp"
#include "skylink/skyRenderer.hpp"
#include "record/sourcePixelFormat.hpp"

// Reads the rtScreenshot render target. The render target table is learned by
// hooking FindRenderTarget(), so no Compositor field offsets are needed; only
// the renderer's texture bookkeeping depends on the game edition.
class FrameGrabber {
public:
  FrameGrabber() = default;

  void SetLayout(
    const RendererLayout *rendererLayout,
    const RenderTargetLayout *renderTargetLayout
  ) {
    m_rendererLayout = rendererLayout;
    m_renderTargetLayout = renderTargetLayout;
  }

  // Supplied by the FindRenderTarget() detour whenever it successfully looks up
  // "rtScreenshot".
  void SetRenderTargets(
    const RenderOutline *barn,
    RenderTarget *targets
  ) {
    m_barn = barn;
    m_targets = targets;
  }

  // Resolve the SystemIO object owned by the compositor.
  static void *GetSystemIO(
    void *compositor,
    const CompositorLayout &layout
  );

  // Read the current rtScreenshot pixels into `out`. `frameIndex` must be the
  // compositor's frame counter as it was before the render call (the render
  // graph uses it to select the ping-pong texture).
  bool Grab(
    u32 frameIndex,
    std::vector<u08> &out,
    i32 &width,
    i32 &height,
    SourcePixelFormat &pixelFormat,
    u32 &gfxFormat,
    u32 &vkFormat
  );

  const std::string &LastError() const { return m_lastError; }

private:
  bool ResolveTarget(RenderTarget **outTarget);
  void Fail(cstring fmt, ...);

  const RenderOutline *m_barn = nullptr;
  RenderTarget *m_targets = nullptr;
  const RendererLayout *m_rendererLayout = nullptr;
  const RenderTargetLayout *m_renderTargetLayout = nullptr;
  std::string m_lastError;
};

#endif
