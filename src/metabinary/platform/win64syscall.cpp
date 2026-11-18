#include <Windows.h>
#include "lowLevel.hpp"

// ----------------------------------------------------------------------------
// [SECTION] OsApi
// ----------------------------------------------------------------------------

namespace Win64SysCall {

uintptr_t GetDlModule(cstring name) {
  return (uintptr_t)::GetModuleHandleA(name);
}

bool ReadMemory(
  uintptr_t addr,
  u08 *dst,
  size_t size
) {
  SIZE_T read = 0;
  if (!::ReadProcessMemory(
    ::GetCurrentProcess(), (void *)addr, dst, size, &read) ||
    read != size)
  {
    return false;
  }
  return true;
}

bool WriteMemory(
  uintptr_t addr,
  const u08 *src,
  size_t size
) {
  SIZE_T written = 0;
  if (!::WriteProcessMemory(
    ::GetCurrentProcess(), (void *)addr, src, size, &written) ||
    written != size)
  {
    return false;
  }
  return true;
}

} // namespace Win64SysCall
