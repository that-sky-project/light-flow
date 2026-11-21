#include "utils/types.hpp"
#include "skylink/skyRenderer.hpp"
#include "skylink/skyTexture.hpp"

usize BytesPerPixel(GfxTexelFormat texelFormat) {
  switch (texelFormat) {
    case kGfxTexelFormat_U8:
    case kGfxTexelFormat_I8:
    case kGfxTexelFormat_U8_sRGB:
      return 1;

    case kGfxTexelFormat_U565:
    case kGfxTexelFormat_U8x2:
    case kGfxTexelFormat_U8x2_int:
    case kGfxTexelFormat_U8x2_sRGB:
    case kGfxTexelFormat_U16_int:
    case kGfxTexelFormat_F16:
      return 2;

    case kGfxTexelFormat_U8x4:
    case kGfxTexelFormat_U8x4_int:
    case kGfxTexelFormat_U8x4_sRGB:
    case kGfxTexelFormat_U8x4_BGRA:
    case kGfxTexelFormat_U8x4_BGRA_sRGB:
    case kGfxTexelFormat_U10_10_10_2:
    case kGfxTexelFormat_U10_10_10_2_BGRA:
    case kGfxTexelFormat_U10_10_10_XR:
    case kGfxTexelFormat_U10_10_10_XR_sRGB:
    case kGfxTexelFormat_U16x2:
    case kGfxTexelFormat_F11_11_10:
    case kGfxTexelFormat_F16x2:
    case kGfxTexelFormat_F32:
    case kGfxTexelFormat_Depth32:
    case kGfxTexelFormat_Depth24Stencil8:
      return 4;

    // 8-byte blocks: ETC2 RGB and EAC R11 use 64-bit 4x4 blocks, BC1/BC4 too.
    case kGfxTexelFormat_U16x4:
    case kGfxTexelFormat_F16x4:
    case kGfxTexelFormat_F32x2:
    case kGfxTexelFormat_ETC2_RGB:
    case kGfxTexelFormat_ETC2_RGB_sRGB:
    case kGfxTexelFormat_EAC_R11_UNORM:
    case kGfxTexelFormat_EAC_R11_SNORM:
    case kGfxTexelFormat_BC1_RGB:
    case kGfxTexelFormat_BC1_RGB_sRGB:
    case kGfxTexelFormat_BC4_R_UNORM:
    case kGfxTexelFormat_BC4_R_SNORM:
      return 8;

    // 16-byte blocks: every ASTC block is 128 bits regardless of its footprint,
    // the other block formats here are 128-bit 4x4 blocks.
    case kGfxTexelFormat_F32x4:
    case kGfxTexelFormat_ETC2_RGBA:
    case kGfxTexelFormat_ETC2_RGBA_sRGB:
    case kGfxTexelFormat_EAC_RG11_UNORM:
    case kGfxTexelFormat_EAC_RG11_SNORM:
    case kGfxTexelFormat_ASTC_RGBA_4x4:
    case kGfxTexelFormat_ASTC_RGBA_5x5:
    case kGfxTexelFormat_ASTC_RGBA_6x6:
    case kGfxTexelFormat_ASTC_RGBA_8x8:
    case kGfxTexelFormat_ASTC_RGBA_4x4_sRGB:
    case kGfxTexelFormat_ASTC_RGBA_5x5_sRGB:
    case kGfxTexelFormat_ASTC_RGBA_6x6_sRGB:
    case kGfxTexelFormat_ASTC_RGBA_8x8_sRGB:
    case kGfxTexelFormat_BC3_RGBA:
    case kGfxTexelFormat_BC3_RGBA_sRGB:
    case kGfxTexelFormat_BC5_RG_UNORM:
    case kGfxTexelFormat_BC5_RG_SNORM:
    case kGfxTexelFormat_BC6_RGBF_UNORM:
    case kGfxTexelFormat_BC6_RGBF_SNORM:
    case kGfxTexelFormat_BC7_RGBA:
    case kGfxTexelFormat_BC7_RGBA_sRGB:
      return 16;

    // Undefined (0) and anything the enum does not know about.
    case kGfxTexelFormat_Undefined:
    default:
      return 0;
  }
}

const Texture *FindRenderTargetTexture(
  const RenderOutline &renderOutline,
  const RenderTarget *renderTargets,
  cstring name,
  u32 frameIndex
) {
  // Sky asserts that the return value of this function is always non-null.
  const RenderTarget *target = FindRenderTarget(renderOutline, renderTargets, name);

  // Multiple render targets with the same name form a chain. The game
  // walks to the last node before reading, so do the same.
  usize chainLength = 0;
  while (target->next && chainLength++ < 64)
    target = target->next;

  // Limits the texture count.
  u32 textureCount = target->*RenderTarget::textureCount;
  if (textureCount == 0) textureCount = 1;
  if (textureCount > 2) textureCount = 2;

  // Get the address of textures array.
  const u08 *textures = reinterpret_cast<const u08 *>(&target->textures);

  // Simulates an array.
  usize offset = static_cast<usize>(frameIndex % textureCount)
    * METABINARY_IMM(, sizeof_Texture).Value();

  return reinterpret_cast<const Texture *>(textures + offset);
}
