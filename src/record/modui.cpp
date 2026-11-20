#include <algorithm>
#include <cstdio>

#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <imgui.h>

#include "record/modui.hpp"
#include "utils/fsUtils.hpp"
#include "utils/utf8Utils.hpp"

// ---------------------------------------------------------------------------
// [SECTION] ImGui
// ---------------------------------------------------------------------------

void LightFlowMod::BuildScene() {
  if (!m_initialized)
    return;

  Update();

  if (m_lightFlowUi)
    m_lightFlowUi->BuildScene();
}

void LightFlowUi::Initialize() {
  LightFlowMod *mod = GetLightFlowMod();
  if (!mod)
    return;

  snprintf(
    m_outputBuffer, sizeof(m_outputBuffer), "%s", mod->m_settings.output.c_str());
}

void LightFlowUi::BuildScene() {
  LightFlowMod *mod = GetLightFlowMod();
  if (!mod)
    return;

  m_DrawOverlay(mod);

  if (m_menuOpen)
    m_DrawPanel(mod);
}

void LightFlowUi::m_DrawOverlay(LightFlowMod *mod) {
  if (!mod->m_capturing || !mod->m_showOverlay)
    return;

  u64 captured = mod->m_capturedFrames;
  u64 dropped = mod->m_droppedFrames;

  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  i64 seconds = std::chrono::duration_cast<std::chrono::seconds>(
    now - mod->m_startTime).count();

  ImGui::PushID("light-flow-overlay");
  ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.55f);

  ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;

  if (ImGui::Begin("##light-flow-rec", nullptr, flags)) {
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "REC");
    ImGui::SameLine();
    ImGui::Text(
      "%02lld:%02lld   %llu frames   %llu dropped",
      static_cast<long long>(seconds / 60),
      static_cast<long long>(seconds % 60),
      static_cast<unsigned long long>(captured),
      static_cast<unsigned long long>(dropped));
  }
  ImGui::End();
  ImGui::PopID();
}

void LightFlowUi::m_DrawPanel(LightFlowMod *mod) {
  static const char *kCodecs[] = {
    "libx264", "h264_mf", "libopenh264", "mpeg4"
  };

  bool open = m_menuOpen;

  ImGui::PushID("light-flow-panel");
  ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Light Flow", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextDisabled("Game profile: %s", GetGameProfileName(mod->m_profile));
    ImGui::Separator();

    ImGui::SetNextItemWidth(-220.0f);
    ImGui::InputText("##output", m_outputBuffer, sizeof(m_outputBuffer));
    if (ImGui::IsItemEdited()) {
      mod->m_settings.output = m_outputBuffer;
      mod->MarkSettingsDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
      m_BrowseOutput(mod);
    ImGui::SameLine();
    if (ImGui::Button("Folder"))
      m_OpenCapturesFolder(mod);

    ImGui::TextDisabled(
      "Empty = <mod folder>\\captures\\light-flow-<time>.mp4. "
      "rtmp://, srt://, udp:// URLs are written directly.");

    ImGui::Spacing();

    std::string codec;
    i32 bitrate = 12000;
    i32 fps = 30;
    i32 queueSize = 4;
    {
      codec = mod->m_settings.codec;
      bitrate = mod->m_settings.bitrateKbps;
      fps = mod->m_settings.targetFps;
      queueSize = mod->m_settings.queueSize;
    }

    int codecIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kCodecs); ++i) {
      if (codec == kCodecs[i]) {
        codecIndex = i;
        break;
      }
    }

    if (ImGui::Combo("Codec", &codecIndex, kCodecs, IM_ARRAYSIZE(kCodecs))) {
      mod->m_settings.codec = kCodecs[codecIndex];
      mod->MarkSettingsDirty();
    }

    if (ImGui::InputInt("Bitrate (kbps)", &bitrate)) {
      bitrate = std::max(100, bitrate);
      mod->m_settings.bitrateKbps = bitrate;
      mod->MarkSettingsDirty();
    }

    if (ImGui::InputInt("Capture FPS (0 = every frame)", &fps)) {
      fps = std::min(240, std::max(0, fps));
      mod->m_targetFps = fps;
      mod->m_settings.targetFps = fps;
      mod->MarkSettingsDirty();
    }

    if (ImGui::SliderInt("Encoder queue", &queueSize, 1, 16)) {
      queueSize = std::min(16, std::max(1, queueSize));
      mod->m_queueLimit = queueSize;
      mod->m_settings.queueSize = queueSize;
      mod->MarkSettingsDirty();
    }

    bool forceMovie = mod->m_forceMovie;
    if (ImGui::Checkbox("Force movie capture mode", &forceMovie)) {
      mod->m_forceMovie = forceMovie;
      mod->m_settings.forceMovieMode = forceMovie;
      mod->MarkSettingsDirty();
    }

    ImGui::SameLine();

    bool showOverlay = mod->m_showOverlay;
    if (ImGui::Checkbox("REC overlay", &showOverlay)) {
      mod->m_showOverlay = showOverlay;
      mod->m_settings.showOverlay = showOverlay;
      mod->MarkSettingsDirty();
    }

    bool swapRedBlue = mod->m_swapRedBlue;
    if (ImGui::Checkbox("Swap R&B", &swapRedBlue)) {
      mod->m_swapRedBlue = swapRedBlue;
      mod->m_settings.swapRedBlue = swapRedBlue;
      mod->MarkSettingsDirty();
    }

    ImGui::Spacing();
    ImGui::Separator();

    bool coreDisabled = mod->IsCoreDisabled();
    ImGui::BeginDisabled(coreDisabled);
    if (ImGui::Button(
      mod->m_capturing ? "Stop recording" : "Start recording",
      ImVec2(220.0f, 0.0f)
    ))
      mod->ToggleCapture();
    ImGui::EndDisabled();

    ImGui::Separator();

    if (mod->m_capturing) {
      i64 seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - mod->m_startTime).count();

      int queued = 0;
      {
        std::lock_guard<std::mutex> lock(mod->m_queueMutex);
        queued = static_cast<int>(mod->m_queue.size());
      }

      ImGui::Text(
        "State: recording%s",
        mod->m_failed.load() ? " (error)" : "");
      ImGui::Text(
        "Resolution: %dx%d",
        mod->m_encoderWidth.load(),
        mod->m_encoderHeight.load());
      ImGui::Text(
        "Captured: %llu   Encoded: %llu   Dropped: %llu",
        static_cast<unsigned long long>(mod->m_capturedFrames),
        static_cast<unsigned long long>(mod->m_encodedFrames.load()),
        static_cast<unsigned long long>(mod->m_droppedFrames));
      ImGui::Text("Queue: %d / %d", queued, mod->m_queueLimit);
      ImGui::Text(
        "Elapsed: %02lld:%02lld",
        static_cast<long long>(seconds / 60),
        static_cast<long long>(seconds % 60));
    } else {
      ImGui::TextUnformatted("State: idle");
    }

    std::string status = mod->GetStatus();
    ImGui::TextWrapped("%s", status.c_str());
  }

  ImGui::End();
  ImGui::PopID();

  m_menuOpen = open;
}

void LightFlowUi::m_BrowseOutput(LightFlowMod *mod) {
  wchar_t file[1024] = {0};
  std::string current(m_outputBuffer);

  if (current.find("://") == std::string::npos && !current.empty()) {
    std::wstring wide = Utf8Utils::ToWide(current);
    wcsncpy_s(file, wide.c_str(), _TRUNCATE);
  } else {
    wcscpy_s(file, L"light-flow.mp4");
  }

  OPENFILENAMEW dialog = {0};
  dialog.lStructSize = sizeof(dialog);
  dialog.lpstrFile = file;
  dialog.nMaxFile = static_cast<DWORD>(countof(file));
  dialog.lpstrFilter =
    L"MP4 Video (*.mp4)\0*.mp4\0Matroska Video (*.mkv)\0*.mkv\0"
    L"All Files (*.*)\0*.*\0";
  dialog.lpstrDefExt = L"mp4";
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

  FsUtils::EnsureDirectory(mod->m_capturesFolder);
  dialog.lpstrInitialDir = mod->m_capturesFolder.c_str();

  if (!GetSaveFileNameW(&dialog))
    return;

  std::string output = Utf8Utils::ToUtf8(file);
  snprintf(m_outputBuffer, sizeof(m_outputBuffer), "%s", output.c_str());

  mod->m_settings.output = output;
  mod->MarkSettingsDirty();
}

void LightFlowUi::m_OpenCapturesFolder(LightFlowMod *mod) {
  FsUtils::EnsureDirectory(mod->m_capturesFolder);
  ShellExecuteW(
    nullptr,
    L"open",
    mod->m_capturesFolder.c_str(),
    nullptr,
    nullptr,
    SW_SHOWNORMAL);
}
