#ifndef __SKYLINK_SKYCOMPOSITOR_HPP__
#define __SKYLINK_SKYCOMPOSITOR_HPP__

#include <string>
#include <vector>

#include "utils/types.hpp"
#include "metabinary/metabinary.hpp"
#include "skylink/skyGfx.hpp"
#include "skylink/skyTypePlaceholders.hpp"

class Compositor {
public:
  METABINARY_LINK_MFN_DECL(void, Compositor, Render, 0, (Scene *, Camera *, Ambience *, Ocean *, JobBarn *, CameraSystem *, SystemUI *))
};



namespace CompositorOffsets {
  // Compositor fields.
  constexpr usize kOutputMode = 12;
  constexpr usize kScreenshotRequested = 88;
  constexpr usize kFrameCounter = 316;
  constexpr usize kSystemIO = 480;
  constexpr usize kRenderTargetBarn = 544;
  constexpr usize kRenderTargetArray = 146696;
}

// VulkanRenderer's texture bookkeeping. These fields are inspected by
// VulkanRenderer::GetPixels(); validating them before calling it avoids its
// internal abort() on non-CPU-readable textures.
namespace RendererOffsets {
  constexpr usize kTextureCapacity = 4112;
  constexpr usize kTextureRecords = 4120;
  constexpr usize kTextureAllocated = 4128;
  constexpr usize kTextureRecordStride = 184;
  constexpr usize kTextureRecordWidth = 100;
  constexpr usize kTextureRecordHeight = 104;
  constexpr usize kTextureRecordTiling = 124;
  // The actual VkFormat after SuitableTextureFormat() workarounds.
  constexpr usize kTextureRecordVkFormat = 96;
  // The logical GfxTexelFormat the texture was requested with.
  constexpr usize kTextureRecordFormat = 176;
  constexpr u32 kTextureTilingLinear = 1;
}

#endif
