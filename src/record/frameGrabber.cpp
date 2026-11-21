#include "record/frameGrabber.hpp"

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
  std::vector<u08> &out,
  i32 &width,
  i32 &height,
  SourcePixelFormat &pixelFormat,
  u32 &gfxFormat,
  u32 &vkFormat
) {
  if (!m_rendererLayout || !m_renderTargetLayout) {
    Fail("renderer layout is not configured");
    return false;
  }

  const Texture *texture = FindRenderTargetTexture(
    *m_renderOutline,
    m_targets,
    "rtScreenshot",
    frameIndex
  );

  // Pick one of the texture's readable slots. MetaBinary does not support const
  // member functions, so we use const_cast.
  u32 slot = const_cast<Texture *>(texture)->GetReadableTexture(0);

  Renderer *renderer = GetRenderer();
  if (!renderer) {
    Fail("renderer is not available");
    return false;
  }

  const RendererLayout &layout = *m_rendererLayout;

  u08 *records = *reinterpret_cast<u08 **>(
    ByteOffset(renderer, layout.textureRecords));
  if (!records) {
    Fail("the renderer texture table is null");
    return false;
  }

  u08 *record = records + static_cast<usize>(slot) * layout.textureRecordStride;

  // The renderer keeps two formats per texture:
  //  - +176: the logical GfxTexelFormat requested by the descriptor
  //  - +96 : the actual VkFormat after SuitableTextureFormat() workarounds
  // The workaround can swap RGBA/BGRA, so the channel order must come from the
  // actual VkFormat.
  gfxFormat = static_cast<u08>(record[layout.textureRecordFormat]);
  vkFormat = *reinterpret_cast<u32 *>(
    record + layout.textureRecordVkFormat);

  width = static_cast<i32>(*reinterpret_cast<u32 *>(
    record + layout.textureRecordWidth));
  height = static_cast<i32>(*reinterpret_cast<u32 *>(
    record + layout.textureRecordHeight));

  SourceFormatInfo logical = GetSourceFormatInfo(gfxFormat);
  SourceFormatInfo actual = GetVkFormatInfo(vkFormat);

  // GetPixels() aborts on an unknown logical format before it copies anything,
  // so refuse the frame instead of letting the game terminate.
  if (logical.bytesPerPixel == 0) {
    Fail("rtScreenshot uses unsupported logical format %u", gfxFormat);
    return false;
  }

  if (actual.bytesPerPixel == 0)
    // unknown VkFormat: fall back to the logical format.
    actual = logical;

  // VulkanRenderer::GetPixels() computes bytesPerRow from the logical format.
  // A workaround that changes the pixel size would make it abort, so reject
  // that combination instead of calling into it.
  if (logical.bytesPerPixel != actual.bytesPerPixel) {
    Fail(
      "rtScreenshot format workaround changed bpp (logical %u=%u, "
      "VkFormat %u=%u)",
      gfxFormat,
      logical.bytesPerPixel,
      vkFormat,
      actual.bytesPerPixel);
    return false;
  }

  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
    Fail("rtScreenshot has invalid size %dx%d", width, height);
    return false;
  }

  usize rowBytes = static_cast<usize>(width) * actual.bytesPerPixel;
  usize frameBytes = rowBytes * static_cast<usize>(height);

  out.resize(frameBytes);

  // Wait for the GPU: pass waitForGpu = 1 so the frame that this render just
  // submitted is fully visible before the CPU copies it out.
  renderer->GetPixels(slot, out.data(), static_cast<i64>(rowBytes), 1);

  pixelFormat = actual.pixelFormat;
  m_lastError.clear();
  return true;
}
