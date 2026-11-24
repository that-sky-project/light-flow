#include "record/video/frameGrabber.hpp"

#include <cstdarg>
#include <cstdio>

#include "skylink/skyRenderer.hpp"
#include "skylink/skyTexture.hpp"

namespace {

inline u08 *ByteOffset(void *base, usize offset) {
  return static_cast<u08 *>(base) + offset;
}

} // namespace

void FrameGrabber::Fail(cstring fmt, ...) {
  char buffer[512];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  m_lastError = buffer;
}

bool FrameGrabber::Grab(
  u32 frameIndex,
  std::vector<u08> *out,
  i32 *width,
  i32 *height,
  SourcePixelFormat *pixelFormat,
  GfxTexelFormat *texelFormat
) {
  const RenderOutline *renderOutline = m_renderOutline.load();
  if (!renderOutline) {
    Fail("render target table is not available yet");
    return false;
  }

  const Texture *texture = FindRenderTargetTexture(
    *renderOutline,
    m_targets.load(),
    "rtScreenshot",
    frameIndex
  );

  // Pick one of the texture's readable slots. MetaBinary does not support const
  // member functions, so we use const_cast.
  i32 slot = const_cast<Texture *>(texture)->GetReadableTexture(0);
  if (slot == -1) {
    Fail("rtScreenshot texture handle invalid: %d", slot);
    return false;
  }

  Renderer *renderer = GetRenderer();
  if (!renderer) {
    Fail("renderer is not available");
    return false;
  }

  i32 w = texture->m_descriptor.width
    , h = texture->m_descriptor.height;
  GfxTexelFormat format = texture->m_descriptor.format;

  *texelFormat = format;
  *width = w;
  *height = h;

  SourceFormatInfo logical = GetSourceFormatInfo(texture->m_descriptor.format);

  if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
    Fail("rtScreenshot has invalid size %dx%d", width, height);
    return false;
  }

  usize rowBytes = static_cast<usize>(w) * BytesPerPixel(format);
  usize frameBytes = rowBytes * static_cast<usize>(h);

  out->resize(frameBytes);

  //auto start = std::chrono::steady_clock::now();

  // Wait for the GPU: pass waitForGpu = 1 so the frame that this render just
  // submitted is fully visible before the CPU copies it out.
  //
  // NOTE(HTMonkeyG): This function has a performance cost: when waitForGpu = 1,
  // the call takes an average of 7 ms; when waitForGpu = 0, it takes an average
  // of 3 ms. Therefore, move it to a worker thread to prevent blocking the main
  // thread and causing a drop in game frame rate.
  renderer->GetPixels(slot, out->data(), static_cast<i64>(rowBytes), 0);

  //auto end = std::chrono::steady_clock::now();
  //auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  //LOGI("GetPixels last for %llu", elapsed.count());

  *pixelFormat = logical.pixelFormat;
  m_lastError.clear();
  return true;
}
