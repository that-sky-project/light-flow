#include "metabinary/metabinary.hpp"
#include "skylink/skyRenderer.hpp"

METABINARY_LINK_MVFN_DEF(
  i64,
  Renderer,
  GetPixels,
  0,
  (u32 texture, void *buffer, i64 bytesPerRow, u08 waitForGpu),
  (texture, buffer, bytesPerRow, waitForGpu)
)

METABINARY_LINK_FN_DEF(
  Renderer *,
  GetRenderer,
  0,
  (),
  ()
)

METABINARY_LINK_FN_DEF(
  RenderTarget *,
  FindRenderTarget,
  0,
  (void *barn, RenderTarget *targets, cstring name),
  (barn, targets, name)
)