#ifndef __RECORD_SOURCEPIXELFORMAT_HPP__
#define __RECORD_SOURCEPIXELFORMAT_HPP__

#include "utils/types.hpp"
#include "skylink/skyGfx.hpp"

// A backend-independent description of the pixels returned by
// VulkanRenderer::GetPixels(). The actual FFmpeg pixel format is resolved in
// the encoder, so this header stays free of FFmpeg includes.
enum SourcePixelFormat: u08 {
  kSourcePixelFormat_Unknown = 0,
  kSourcePixelFormat_RGBA8,
  kSourcePixelFormat_BGRA8,
  kSourcePixelFormat_RGB10A2,
  kSourcePixelFormat_BGR10A2,
  kSourcePixelFormat_RGBAF16,
  kSourcePixelFormat_RGBAF32
};

struct SourceFormatInfo {
  u32 bytesPerPixel;
  SourcePixelFormat pixelFormat;
  cstring name;
};

// Logical GfxTexelFormat (as stored in the texture descriptor).
// Tonemap_movie uses the "BGR" shader define, so its output is stored with the
// red and blue channels exchanged relative to the render target format.
inline SourcePixelFormat SwapRedBlue(SourcePixelFormat format) {
  switch (format) {
    case kSourcePixelFormat_RGBA8:
      return kSourcePixelFormat_BGRA8;
    case kSourcePixelFormat_BGRA8:
      return kSourcePixelFormat_RGBA8;
    case kSourcePixelFormat_RGB10A2:
      return kSourcePixelFormat_BGR10A2;
    case kSourcePixelFormat_BGR10A2:
      return kSourcePixelFormat_RGB10A2;
    default:
      return format;
  }
}

inline SourceFormatInfo GetSourceFormatInfo(u32 format) {
  switch (format) {
    case kGfxTexelFormat_U8x4:
    case kGfxTexelFormat_U8x4_int:
    case kGfxTexelFormat_U8x4_sRGB:
      return {4, kSourcePixelFormat_RGBA8, "U8x4"};
    case kGfxTexelFormat_U8x4_BGRA:
    case kGfxTexelFormat_U8x4_BGRA_sRGB:
      return {4, kSourcePixelFormat_BGRA8, "U8x4_BGRA"};
    case kGfxTexelFormat_U10_10_10_2:
    case kGfxTexelFormat_U10_10_10_XR:
    case kGfxTexelFormat_U10_10_10_XR_sRGB:
      return {4, kSourcePixelFormat_BGR10A2, "U10_10_10_2"};
    case kGfxTexelFormat_U10_10_10_2_BGRA:
      return {4, kSourcePixelFormat_RGB10A2, "U10_10_10_2_BGRA"};
    case kGfxTexelFormat_F16x4:
      return {8, kSourcePixelFormat_RGBAF16, "F16x4"};
    case kGfxTexelFormat_F32x4:
      return {16, kSourcePixelFormat_RGBAF32, "F32x4"};
    default:
      return {0, kSourcePixelFormat_Unknown, "unsupported"};
  }
}

// Vulkan VkFormat values used by VulkanRenderer::CreateTexture(). The renderer
// can substitute a different format when the requested one is unavailable
// (SuitableTextureFormat), so the channel order must be taken from here rather
// than from the logical GfxTexelFormat.
inline SourceFormatInfo GetVkFormatInfo(u32 vkFormat) {
  // VK_FORMAT_R8G8B8A8_* (UNORM/SNORM/USCALED/SSCALED/UINT/SINT/SRGB).
  if (vkFormat >= 37 && vkFormat <= 43)
    return {4, kSourcePixelFormat_RGBA8, "R8G8B8A8"};

  // VK_FORMAT_B8G8R8A8_*.
  if (vkFormat >= 44 && vkFormat <= 50)
    return {4, kSourcePixelFormat_BGRA8, "B8G8R8A8"};

  // VK_FORMAT_A8B8G8R8_*_PACK32: little-endian byte order is R,G,B,A.
  if (vkFormat >= 51 && vkFormat <= 57)
    return {4, kSourcePixelFormat_RGBA8, "A8B8G8R8"};

  // VK_FORMAT_A2R10G10B10_*_PACK32: low bits are R.
  if (vkFormat >= 58 && vkFormat <= 63)
    return {4, kSourcePixelFormat_BGR10A2, "A2R10G10B10"};

  // VK_FORMAT_A2B10G10R10_*_PACK32: low bits are B.
  if (vkFormat >= 64 && vkFormat <= 69)
    return {4, kSourcePixelFormat_RGB10A2, "A2B10G10R10"};

  // VK_FORMAT_R16G16B16A16_SFLOAT.
  if (vkFormat == 97)
    return {8, kSourcePixelFormat_RGBAF16, "R16G16B16A16_SFLOAT"};

  // VK_FORMAT_R32G32B32A32_SFLOAT.
  if (vkFormat == 109)
    return {16, kSourcePixelFormat_RGBAF32, "R32G32B32A32_SFLOAT"};

  return {0, kSourcePixelFormat_Unknown, "unsupported VkFormat"};
}

#endif
