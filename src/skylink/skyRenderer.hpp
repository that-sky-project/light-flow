#ifndef __SKYLINK_SKYRENDERER_HPP__
#define __SKYLINK_SKYRENDERER_HPP__

#include <cstddef>

#include "utils/types.hpp"
#include "metabinary/metabinary.hpp"
#include "skylink/skyTexture.hpp"

class RenderOutline;

struct RenderTarget {
  RenderTarget *next;
  Texture texture[2];
  u08 textureCount;
  u08 _pad0[7];
};

// Layout of RenderTarget for each edition. The two ping-pong Texture slots
// have different sizes, so textureCount and the second texture move as well.
struct RenderTargetLayout {
  usize textureOffset;
  usize textureStride;
  usize textureCount;
};

// Sky_dump (NetEase) uses 128-byte texture slots.
inline constexpr RenderTargetLayout kRenderTargetLayoutNetease = {
  /* textureOffset */ 8,
  /* textureStride */ 128,
  /* textureCount */ 264,
};

// Sky-32.2-260129 (TGC) uses 104-byte texture slots.
inline constexpr RenderTargetLayout kRenderTargetLayoutTgc = {
  /* textureOffset */ 8,
  /* textureStride */ 104,
  /* textureCount */ 216,
};

class Renderer {
public:
  virtual ~Renderer() = default;

  METABINARY_LINK_MVFN(i64, Renderer, GetPixels, 0,
    (u32 texture, void *buffer, i64 bytesPerRow, u08 waitForGpu),
    (texture, buffer, bytesPerRow, waitForGpu)
  )
};

METABINARY_LINK_FN(Renderer *, GetRenderer, 0, (), ())

METABINARY_LINK_FN(RenderTarget *, FindRenderTarget, 0,
  (const RenderOutline *barn, RenderTarget *targets, cstring name),
  (barn, targets, name)
)

#endif
