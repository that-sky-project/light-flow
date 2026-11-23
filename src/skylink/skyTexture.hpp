#ifndef __SKYLINK_SKYTEXTURE_HPP__
#define __SKYLINK_SKYTEXTURE_HPP__

#include "utils/types.hpp"
#include "metabinary/metabinary.hpp"
#include "skylink/skyGfx.hpp"

struct TextureDescriptor {
  GfxTextureType type;
  GfxTexelFormat format;
  u08 sampleCount;
  u08 _1;
  u16 _2;
  u16 width;
  u16 height;
  u16 depth;
  u08 mipmapLevel;
  u08 _3;
  u08 layerCount;
};

struct Texture {
  TextureDescriptor m_descriptor;
  // INCOMPLETE

  METABINARY_LINK_MFN(i32, Texture, GetReadableTexture, 0, (i32 index), (index))
};

METABINARY_LINK_IMM(sizeof_Texture)

#endif
