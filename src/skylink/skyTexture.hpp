#ifndef __SKYLINK_SKYTEXTURE_HPP__
#define __SKYLINK_SKYTEXTURE_HPP__

#include "utils/types.hpp"
#include "metabinary/metabinary.hpp"

struct TextureDescriptor {
  u08 type;
  u08 format;        // GfxTexelFormat
  u08 sampleCount;
  u08 unk_1;
  u16 unk_2;
  u16 width;
  u16 height;
  u16 depth;
  u08 mipmapLevel;
  u08 unk_3;
  u08 layerCount;
  char unk_4[9];
};

struct TextureDebugInfo {
  u64 unk[3];
};

struct Texture {
  TextureDescriptor m_descriptor;
  i32 m_textures[3];
  i32 m_buffer;
  u32 m_imageMemSize;
  u08 m_usage;
  char m_flags[2];
  u08 _pad0;
  TextureDebugInfo m_debugInfo;
  char m_name[29];
  u08 unk_6;
  i16 unk_7;

  // Linked through MetaBinary. MetaBinarySystem assigns the address from
  // BinaryDescriptor.json; the macro keeps the call site looking like a normal
  // member call.
  METABINARY_LINK_MFN_DECL(u32, Texture, GetReadableTexture, 0, (i32 index))
};

#endif
