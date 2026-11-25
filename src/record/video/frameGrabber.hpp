#ifndef __RECORD_FRAMEGRABBER_HPP__
#define __RECORD_FRAMEGRABBER_HPP__

#include <atomic>
#include <string>
#include <vector>

#include "utils/types.hpp"
#include "skylink/skyCompositor.hpp"
#include "skylink/skyRenderer.hpp"
#include "record/video/sourcePixelFormat.hpp"

// Reads the rtScreenshot render target. The render target table is learned by
// hooking FindRenderTarget(), so no Compositor field offsets are needed; only
// the renderer's texture bookkeeping depends on the game edition.
//
// SetRenderTargets() runs on the game thread (from the FindRenderTarget hook),
// while Grab() runs on the dedicated grab worker, so the cached table pointers
// are atomic. Grab() itself and LastError() must stay on one thread.
class FrameGrabber {
public:
  FrameGrabber() = default;

  // Supplied by the FindRenderTarget() detour whenever it successfully looks up
  // "rtScreenshot".
  inline void SetRenderTargets(
    const RenderOutline *renderOutline,
    const RenderTarget *targets
  ) {
    // WARN: The game calls FindRenderTarget in the main render loop and on window
    // resize. When the window is resized, the game first constructs RenderOutline
    // on the stack, then passes it to FindRenderTarget and other functions for
    // initialization, and finally assigns it to Compositor::m_renderOutline via
    // operator=. The game also executes the window resize logic once during its
    // first initialization. Therefore, caching the pointer only once is wrong and
    // very likely to cache a stack object, causing a crash.
    //
    // So we cache on every call, so that it gets corrected when the correct object
    // address arrives.
    m_renderOutline.store(renderOutline);
    m_targets.store(targets);

    // if (!m_isRenderTargetSet) {
    //   m_targets = targets;
    //   m_isRenderTargetSet = true;
    // }
    // LOG("Cached RenderTarget(s): 0x%p", m_targets);
  }

  // Read the current rtScreenshot pixels into `out`. `frameIndex` must be the
  // compositor's frame counter as it was before the render call (the render
  // graph uses it to select the ping-pong texture). Called on the grab worker.
  bool Grab(
    u32 frameIndex,
    std::vector<u08> *out,
    i32 *width,
    i32 *height,
    SourcePixelFormat *pixelFormat,
    GfxTexelFormat *texelFormat
  );

  const std::string &LastError() const { return m_lastError; }

private:
  bool ResolveTarget(RenderTarget **outTarget);
  void Fail(cstring fmt, ...);

  bool m_isRenderOutlineSet = false;
  bool m_isRenderTargetSet = false;
  std::atomic<const RenderOutline *> m_renderOutline{nullptr};
  std::atomic<const RenderTarget *> m_targets{nullptr};
  std::string m_lastError;
};

#endif
