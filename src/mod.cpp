#include <htmodloader/htmod.h>
#include <imgui.h>

__declspec(dllexport) HTStatus HTMLAPI HTModOnInit(
  void *
) {
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
