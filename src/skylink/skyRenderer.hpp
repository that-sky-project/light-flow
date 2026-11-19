#ifndef __SKYLINK_SKYRENDERER_HPP__
#define __SKYLINK_SKYRENDERER_HPP__

#include "utils/types.hpp"
#include "metabinary/metabinary.hpp"
#include "skylink/skyTexture.hpp"

struct RenderTarget {
  RenderTarget *next;
  Texture texture[2];
  u08 textureCount;
  u08 _pad0[7];
};

class Renderer {
public:
  virtual ~Renderer() = default;

  METABINARY_LINK_MVFN_DECL(i64, Renderer, GetPixels, 0, (u32, void *,i64, u08))
};

Renderer *GetRenderer();
RenderTarget *FindRenderTarget(
  void *barn,
  RenderTarget *targets,
  cstring name
);

#endif
