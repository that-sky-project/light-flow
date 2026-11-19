#ifndef __SKYLINK_SKYRENDERER_HPP__
#define __SKYLINK_SKYRENDERER_HPP__

#include "utils/types.hpp"
#include "metabinary/metabinary.hpp"
#include "skylink/skyTexture.hpp"

class RenderOutline;

struct RenderTarget {
  RenderTarget *next;
  Texture texture[2];
  u08 textureCount;
  u08 _pad0[7];

  Texture *GetTexture(u32 index);
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
  (const RenderOutline &barn, RenderTarget *targets, cstring name),
  (barn, targets, name)
)

#endif
