#include <string>
#include <htmodloader/htmod.h>
#include <imgui.h>
#include "utils/types.hpp"
#include "utils/logger.hpp"
#include "metabinary/platform/lowLevel.hpp"
#include "metabinary/binaryDescriptor.hpp"
#include "metabinary/metabinary.hpp"

std::wstring GetModuleFolder(HMODULE module) {
  wchar_t path[MAX_PATH] = {0};
  DWORD length = GetModuleFileNameW(module, path, MAX_PATH);

  if (length == 0)
    return L".";

  std::wstring result(path, length);
  usize separator = result.find_last_of(L"\\/");
  if (separator == std::wstring::npos)
    return L".";

  return result.substr(0, separator);
}

std::string ReadFileString(const std::wstring &path) {
  FILE *file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
    return std::string();

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  std::string data;
  if (size > 0) {
    data.resize(static_cast<usize>(size));
    fread(&data[0], 1, static_cast<usize>(size), file);
  }

  fclose(file);
  return data;
}

__declspec(dllexport) HTStatus HTMLAPI HTModOnInit(
  void *
) {
  std::wstring folder = GetModuleFolder(HTModLoader::GetCurrentModuleHandle());

  std::wstring logPath = folder + L"\\light-flow.log";
  Logger *logger = new Logger();
  logger->Initialize(logPath);

  LOG("------------------------------------------------------------");
  LOG("<-- Light Flow -->");
  LOG("  Copyright (c) 2026 That Sky Project, HTMonkeyG");
  LOG("  High quality in-game recording mod for Sky: Children of the light");
  LOG("  https://www.github.com/that-sky-project/light-flow");
  LOG("------------------------------------------------------------");

  SetAssertHandler([](cstring expr, cstring msg, cstring file, u32 line, u32) -> void {
    LOGE("ASSERT! %s (%u): %s", file, line, expr);
    LOGE("ASSERT! %s", msg);
  });

  std::wstring path = folder + L"\\Data\\Manifests\\BinaryDescriptor.json";
  std::string json = ReadFileString(path);

  if (json.empty()) {
    LOGE("Cannot read Data\\Manifests\\BinaryDescriptor.json");
    return HT_FAIL;
  }

  auto *binaryDescriptorBarn = new BinaryDescriptorBarn();
  binaryDescriptorBarn->Initialize(json.c_str());
  LOG("Loaded BinaryDescriptor.json from disk - %llu bytes", json.length());

  auto *metaBinarySystem = new MetaBinarySystem();
  metaBinarySystem->Initialize(binaryDescriptorBarn);
  if (!metaBinarySystem->IsValid()) {
    LOG("Errors occured when initializing MetaBinarySystem, the mod has been shut down.");
    return HT_FAIL;
  }
  LOG("MetaBinarySystem initialized");

  return HT_SUCCESS;
}

__declspec(dllexport) void HTMLAPI HTModRenderGui(
  float timeElapesed,
  void *
) {
  static bool s_first = true;
  if (s_first) {
    HTImGuiContextsEx ctxex;
    HTImGuiDispatchEx(&ctxex);

    ImGui::SetCurrentContext((ImGuiContext *)ctxex.ctx.pImGui);
    ImGui::SetAllocatorFunctions(
      (ImGuiMemAllocFunc)ctxex.ctx.pImAllocatorAllocFunc,
      (ImGuiMemFreeFunc)ctxex.ctx.pImAllocatorFreeFunc,
      ctxex.ctx.pImAllocatorUserData
    );
  }

  (void)timeElapesed;
}
