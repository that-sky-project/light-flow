#include <Windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <htmodloader/htmodloader.h>
#include "lowLevel.hpp"

// ----------------------------------------------------------------------------
// [SECTION] LowLevel
// ----------------------------------------------------------------------------

// Since HTModLoader does not have bitwise mask, so we consider every mask byte
// that is not 0xFF to be "?".
static TgcString s_BuildHTPattern(
  const u08 *pattern,
  const u08 *masks,
  u32 length
) {
  constexpr cstring kHexChars = "0123456789ABCDEF";
  TgcString result(length * 3, '\0');

  for (u32 i = 0; i < length; i++) {
    if (masks[i] != 0xFF) {
      result[i * 3] = '?';
      result[i * 3 + 1] = ' ';
    } else {
      result[i * 3] = kHexChars[(pattern[i] >> 4) & 0xF];
      result[i * 3 + 1] = kHexChars[pattern[i] & 0xF];
    }
    result[i * 3 + 2] = ' ';
  }

  return result;
}

static HMODULE g_hModule = nullptr;

namespace HTModLoader {

HMODULE GetCurrentModuleHandle() {
  return g_hModule;
}

void SetCurrentModuleHandle(HMODULE hModule) {
  g_hModule = hModule;
}

bool LoadSetting(cstring key, TgcString *result) {
  if (result == nullptr)
    return false;

  HMODULE hModule = HTModLoader::GetCurrentModuleHandle();

  UINT32 cch = 0;
  HTStatus status = HTOptionGetCustom(hModule, key, HTOptionType_String, nullptr, &cch);
  if (status != HT_SUCCESS || cch == 0)
    return false;

  TgcString buffer(static_cast<size_t>(cch), '\0');

  UINT32 written = cch;
  status = HTOptionGetCustom(hModule, key, HTOptionType_String, buffer.data(), &written);
  if (status != HT_SUCCESS)
    return false;

  *result = buffer;
  return true;
}

bool LoadSetting(cstring key, bool *result) {
  if (result == nullptr)
    return false;

  HMODULE hModule = HTModLoader::GetCurrentModuleHandle();

  bool buffer;
  HTStatus status = HTOptionGetCustom(hModule, key, HTOptionType_Bool, &buffer, nullptr);
  if (status != HT_SUCCESS)
    return false;

  *result = buffer;
  return true;
}

bool LoadSetting(cstring key, f64 *result) {
  if (result == nullptr)
    return false;

  HMODULE hModule = HTModLoader::GetCurrentModuleHandle();

  f64 buffer;
  HTStatus status = HTOptionGetCustom(hModule, key, HTOptionType_Double, &buffer, nullptr);
  if (status != HT_SUCCESS)
    return false;

  *result = buffer;
  return true;
}

bool LoadSetting(cstring key, i32 *result) {
  f64 buffer;
  if (!LoadSetting(key, &buffer))
    return false;
  *result = (i32)buffer;
  return true;
}

bool SaveSetting(cstring key, const TgcString &value) {
  HMODULE hModule = HTModLoader::GetCurrentModuleHandle();
  return HTOptionSetCustom(hModule, key, HTOptionType_String, value.c_str()) == HT_SUCCESS
    ? true
    : false;
}

bool SaveSetting(cstring key, bool value) {
  HMODULE hModule = HTModLoader::GetCurrentModuleHandle();
  return HTOptionSetCustom(hModule, key, HTOptionType_Bool, &value) == HT_SUCCESS
    ? true
    : false;
}

bool SaveSetting(cstring key, f64 value) {
  HMODULE hModule = HTModLoader::GetCurrentModuleHandle();
  return HTOptionSetCustom(hModule, key, HTOptionType_Double, &value) == HT_SUCCESS
    ? true
    : false;
}

bool SaveSetting(cstring key, i32 value) {
  f64 buffer = value;
  HMODULE hModule = HTModLoader::GetCurrentModuleHandle();
  return HTOptionSetCustom(hModule, key, HTOptionType_Double, &buffer) == HT_SUCCESS
    ? true
    : false;
}

uintptr_t ScanPattern(
  uintptr_t module,
  const u08 *pattern,
  const u08 *masks,
  u32 length
) {
  (void)module;
  TgcString s = s_BuildHTPattern(pattern, masks, length);
  HTAsmSig sig = { s.c_str(), HTSigScanType_Direct, 0 };
  return (uintptr_t)::HTSigScan(&sig);
}

uintptr_t CreateHook(
  cstring tag,
  uintptr_t func,
  uintptr_t detour,
  uintptr_t *trampoline
) {
  HMODULE hModule = GetCurrentModuleHandle();
  LowLevelErrorCheck(hModule, "Cannot find the current module handle");

  HTAsmFunction sfn = { tag, (void *)func, (void *)detour, nullptr };
  HTStatus s = ::HTAsmHookCreate(hModule, &sfn);
  LowLevelErrorCheck(
    s == HT_SUCCESS,
    "Error: %d at call %s, exiting app..",
    ::HTGetLastError(),
    "CreateHook()");

  *trampoline = (uintptr_t)sfn.origin;
  return (uintptr_t)sfn.fn;
}

void DeleteHook(
  uintptr_t hHook
) {
  (void)hHook;
  // HTModLoader didn't provide any API to remove a hook.
}

void EnableHook(
  uintptr_t hHook
) {
  HMODULE hModule = GetCurrentModuleHandle();
  HTStatus s = ::HTAsmHookEnable(hModule, (void *)hHook);
  LowLevelErrorCheck(
    s == HT_SUCCESS,
    "Error: %d at call %s, exiting app..",
    ::HTGetLastError(),
    "EnableHook()");
}

void DisableHook(
  uintptr_t hHook
) {
  HMODULE hModule = GetCurrentModuleHandle();
  HTStatus s = ::HTAsmHookDisable(hModule, (void *)hHook);
  LowLevelErrorCheck(
    s == HT_SUCCESS,
    "Error: %d at call %s, exiting app..",
    ::HTGetLastError(),
    "DisableHook()");
}

} // namespace HTModLoader

void LowLevelErrorHandler(
  cstring file,
  int line,
  cstring msg,
  ...
) {
  char buffer[1024];

  va_list va;
  va_start(va, msg);
  vsnprintf(buffer, sizeof(buffer), msg, va);
  va_end(va);

  HTTellText("[light-flow][LowLevel] %s (%d): %s", file, line, buffer);
}
