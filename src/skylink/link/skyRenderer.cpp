#include "skylink/skyRenderer.hpp"

Texture *RenderTarget::GetTexture(u32 index) {
  RenderTarget *owner = this;
  while (owner->next)
    owner = owner->next;

  u32 count = owner->textureCount;
  if (count == 0)
    return nullptr;

  return &owner->texture[index % count];
}
