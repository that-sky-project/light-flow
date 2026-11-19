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

void *FrameGrabber::GetSystemIO(void *compositor) {
  if (!compositor)
    return nullptr;
  return *reinterpret_cast<void **>(
    ByteOffset(compositor, CompositorOffsets::kSystemIO));
}

bool FrameGrabber::ResolveTarget(RenderTarget **outTarget) {
  if (!m_compositor) {
    Fail("compositor is not available yet");
    return false;
  }

  void *barn = ByteOffset(m_compositor, CompositorOffsets::kRenderTargetBarn);
  RenderTarget *targets = reinterpret_cast<RenderTarget *>(
    ByteOffset(m_compositor, CompositorOffsets::kRenderTargetArray));

  RenderTarget *target = FindRenderTarget(barn, targets, "rtScreenshot");
  if (!target) {
    Fail("rtScreenshot is not registered (unsupported graphics feature?)");
    return false;
  }

  *outTarget = target;
  return true;
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
  RenderTarget *target = nullptr;

  if (!ResolveTarget(&target))
    return false;

  // Multiple render targets with the same name form a chain. TakeScreenshot()
  // walks to the last node before reading, so do the same.
  usize chainLength = 0;
  while (target->next && chainLength++ < 64)
    target = target->next;

  u32 textureCount = target->textureCount;
  if (textureCount == 0)
    textureCount = 1;
  if (textureCount > countof(target->texture))
    textureCount = static_cast<u32>(countof(target->texture));

  Texture &texture = target->texture[frameIndex % textureCount];

  // Texture::GetReadableTexture(): pick one of the texture's readable slots.
  u32 slot = texture.GetReadableTexture(0);

  Renderer *renderer = GetRenderer();
  if (!renderer) {
    Fail("renderer is not available");
    return false;
  }

  // MetaBinaryVirtualFunction needs the real object's vftable to resolve the
  // slot. The renderer is a process-wide singleton, so cache it once.
  static Renderer *s_cachedRenderer = nullptr;
  if (!s_cachedRenderer) {
    Renderer::sfn_GetPixels_0.Cache(renderer);
    s_cachedRenderer = renderer;
  } else if (s_cachedRenderer != renderer) {
    Fail("renderer object changed");
    return false;
  }

  // Mirror the validation that VulkanRenderer::GetPixels() performs before it
  // reads memory. Without this, an unsupported texture would call abort().
  u32 capacity = *reinterpret_cast<u32 *>(
    ByteOffset(renderer, RendererOffsets::kTextureCapacity));
  u32 *allocated = *reinterpret_cast<u32 **>(
    ByteOffset(renderer, RendererOffsets::kTextureAllocated));
  u08 *records = *reinterpret_cast<u08 **>(
    ByteOffset(renderer, RendererOffsets::kTextureRecords));

  if (slot >= capacity || !allocated || !records) {
    Fail("rtScreenshot texture slot %u is invalid", slot);
    return false;
  }
  if ((allocated[slot >> 5] & (1u << (slot & 31))) == 0) {
    Fail("rtScreenshot texture slot %u is not allocated", slot);
    return false;
  }

  u08 *record = records +
                static_cast<usize>(slot) * RendererOffsets::kTextureRecordStride;
  u32 tiling = *reinterpret_cast<u32 *>(
    record + RendererOffsets::kTextureRecordTiling);
  if (tiling != RendererOffsets::kTextureTilingLinear) {
    Fail("rtScreenshot texture is not CPU readable (tiling %u)", tiling);
    return false;
  }

  // The renderer keeps two formats per texture:
  //  - +176: the logical GfxTexelFormat requested by the descriptor
  //  - +96 : the actual VkFormat after SuitableTextureFormat() workarounds
  // The workaround can swap RGBA/BGRA, so the channel order must come from the
  // actual VkFormat.
  gfxFormat = static_cast<u08>(
    *(record + RendererOffsets::kTextureRecordFormat));
  vkFormat = *reinterpret_cast<u32 *>(
    record + RendererOffsets::kTextureRecordVkFormat);

  width = static_cast<i32>(*reinterpret_cast<u32 *>(
    record + RendererOffsets::kTextureRecordWidth));
  height = static_cast<i32>(*reinterpret_cast<u32 *>(
    record + RendererOffsets::kTextureRecordHeight));

  SourceFormatInfo logical = GetSourceFormatInfo(gfxFormat);
  SourceFormatInfo actual = GetVkFormatInfo(vkFormat);

  if (actual.bytesPerPixel == 0)
    actual = logical; // unknown VkFormat: fall back to the logical format

  if (actual.bytesPerPixel == 0) {
    Fail(
      "rtScreenshot uses unsupported format (logical %u, VkFormat %u)",
      gfxFormat,
      vkFormat);
    return false;
  }

  // VulkanRenderer::GetPixels() computes bytesPerRow from the logical format.
  // A workaround that changes the pixel size would make it abort, so reject
  // that combination instead of calling into it.
  if (logical.bytesPerPixel != 0 &&
      logical.bytesPerPixel != actual.bytesPerPixel) {
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
  renderer->GetPixels(
    slot,
    out.data(),
    static_cast<i64>(rowBytes),
    1);

  pixelFormat = actual.pixelFormat;
  m_lastError.clear();
  return true;
}
