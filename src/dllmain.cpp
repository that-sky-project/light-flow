#include <Windows.h>
#include "utils/loaderUtils.hpp"

BOOL APIENTRY DllMain(
  HMODULE hModule,
  DWORD reason,
  LPVOID
) {
  if (reason == DLL_PROCESS_ATTACH) {
    HTModLoader::SetCurrentModuleHandle(hModule);
    DisableThreadLibraryCalls(hModule);
  }
  return TRUE;
}
