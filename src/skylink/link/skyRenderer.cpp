#include "utils/types.hpp"
#include "skylink/skyRenderer.hpp"
#include "skylink/skyTexture.hpp"

usize BytesPerPixel(GfxTexelFormat texelFormat) {

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
