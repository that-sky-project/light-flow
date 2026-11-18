#include <Windows.h>
#include <math.h>
#include <htmodloader/htmod.h>
#include "utils/types.hpp"

class Compositor { };
class SystemIO { };
class RenderOutline { };
class RenderTarget { };
class Texture { };
class VulkanRenderer { };

HMODULE hModuleDll;

/*template<typename T>
struct As {
  template<typename Val>
  As(Val *val, intptr_t offset): result(reinterpret_cast<T *>(reinterpret_cast<u08 *>(val) + offset)) { }
  operator T &() const { return *result; }
  T *result;
};*/

template<typename Val, typename T> inline Val &As(T *src, intptr_t offset = 0) noexcept { return *reinterpret_cast<Val *>(reinterpret_cast<u08 *>(src) + offset); }

const HTAsmSig sigE8_Compositor_Render = {
  "4C 8B 8E ?  ?  ?  ?  48 8B 96 ?  ?  ?  ?  E8 ?  "
  "?  ?  ?  48 8B 8E ?  ?  ?  ?  E8 ",
  HT_SCAN_E8,
  0x0E
};

HTAsmFunction sfn_Screenshot_GetShareableResolution = {
  "Screenshot::GetShareableResolution()",
  nullptr,
  nullptr,
  nullptr
};

HTAsmFunction sfn_SystemIO_IsCapturingMovie = {
  "SystemIO::IsCapturingMovie()",
  nullptr,
  nullptr,
  nullptr
};

HTAsmFunction sfn_Compositor_Render = {
  "Compositor::Render()",
  nullptr,
  nullptr,
  nullptr
};

HTAsmFunction sfn_VulkanRenderer_GetPixels = {
  "VulkanRenderer::GetPixels()",
  nullptr,
  nullptr,
  nullptr
};

HTAsmFunction sfn_FindRenderTarget = {
  "FindRenderTarget()",
  nullptr,
  nullptr,
  nullptr
};

HTAsmFunction sfn_Texture_GetReadableTexture = {
  "Texture::GetReadableTexture()",
  nullptr,
  nullptr,
  nullptr
};

HTStatus createAndEnableHook(
  const HTAsmSig *sigXX,
  HTAsmFunction *sfn
) {
  if (sigXX && !HTSigScanFunc(sigXX, sfn))
    return HT_FAIL;

  if (!HTAsmHookCreate(hModuleDll, sfn))
    return HT_FAIL;

  return HTAsmHookEnable(hModuleDll, sfn->fn);
}

RenderTarget *FindRenderTarget(
  const RenderOutline &outline,
  const RenderTarget *renderTargets,
  cstring name
) {
  return ((decltype(&FindRenderTarget))sfn_FindRenderTarget.fn)(
    outline, renderTargets, name);
}

i32 Texture_GetReadableTexture(
  Texture *pThis,
  i32 index
) {
  return ((decltype(&Texture_GetReadableTexture))sfn_Texture_GetReadableTexture.fn)(
    pThis, index);
}

void *VulkanRenderer_GetPixels(
  VulkanRenderer *pThis,
  i32 texture,
  void *buffer,
  u64 bytesPerRow,
  bool waitForGpu
) {
  return ((decltype(&VulkanRenderer_GetPixels))sfn_VulkanRenderer_GetPixels.fn)(
    pThis, texture, buffer, bytesPerRow, waitForGpu);
}

bool hook_SystemIO_IsCapturingMovie(
  SystemIO *pThis
) {
  return true;
  return ((decltype(&hook_SystemIO_IsCapturingMovie))sfn_SystemIO_IsCapturingMovie.origin)(
    pThis);
}

void hook_Compositor_Render(
  Compositor *pThis,
  void *a2,
  void *a3,
  void *a4
) {
  ((decltype(&hook_Compositor_Render))sfn_Compositor_Render.origin)(
    pThis, a2, a3, a4);

  HTTellText("Compositor::m_outputMode = %d", As<i32>(pThis, 0x1024));
  /*RenderTarget *rt = FindRenderTarget(
    As<RenderOutline>(pThis, 6320),
    As<RenderTarget *>(pThis, 337016),
    "rtScreenshot");*/
  //HTTellText("rtScreenshot = %p", rt);
  //HTTellText("aa %p %p", &As<RenderOutline>(pThis, 6320), &As<RenderOutline>(pThis, 337016));
}

__declspec(dllexport) HTStatus HTMLAPI HTModOnInit(
  void *
) {
  sfn_Compositor_Render.detour = (void *)hook_Compositor_Render;
  createAndEnableHook(&sigE8_Compositor_Render, &sfn_Compositor_Render);

  //sfn_SystemIO_IsCapturingMovie.fn = (void *)0x00007FF78E82D510ull;
  sfn_SystemIO_IsCapturingMovie.fn = (void *)0x00007FF78E82D590ull;
  sfn_SystemIO_IsCapturingMovie.detour = (void *)hook_SystemIO_IsCapturingMovie;
  HTTellText("fn: %p, detour: %p", sfn_SystemIO_IsCapturingMovie.fn, sfn_SystemIO_IsCapturingMovie.detour);
  HTStatus s = createAndEnableHook(nullptr, &sfn_SystemIO_IsCapturingMovie);
  HTTellText("S: %d, ERRNO: %d", (i32)s, HTGetLastError());

  sfn_FindRenderTarget.fn = (void *)0x00007FF78E0BEA90ull;
  sfn_Texture_GetReadableTexture.fn = (void *)0x00007FF78CA9AB60ull;
  sfn_VulkanRenderer_GetPixels.fn = (void *)0x00007FF78E819A40ull;

  return HT_SUCCESS;
}

__declspec(dllexport) HTStatus HTMLAPI HTModOnEnable(
  void *
) {
  return HT_SUCCESS;
}

BOOL APIENTRY DllMain(
  HMODULE hModule,
  DWORD dwReason,
  LPVOID lpReserved
) {
  if (dwReason == DLL_PROCESS_ATTACH)
    hModuleDll = hModule;
  return TRUE;
}