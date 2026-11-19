#include "lightFlow.hpp"

#include <algorithm>
#include <commdlg.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <shellapi.h>

#include <htmodloader/htmodloader.h>
#include <imgui.h>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace LightFlow {

LightFlowMod *g_lightFlow = nullptr;
thread_local bool g_forceMovieThisRender = false;

namespace {

inline u08 *At(void *base, usize offset) {
  return static_cast<u08 *>(base) + offset;
}

inline const u08 *At(const void *base, usize offset) {
  return static_cast<const u08 *>(base) + offset;
}

std::wstring Utf8ToWide(const std::string &text) {
  if (text.empty())
    return std::wstring();

  int length = MultiByteToWideChar(
    CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0)
    return std::wstring();

  std::wstring result(static_cast<usize>(length), L'\0');
  MultiByteToWideChar(
    CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], length);
  return result;
}

std::string WideToUtf8(const std::wstring &text) {
  if (text.empty())
    return std::string();

  int length = WideCharToMultiByte(
    CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
    nullptr, 0, nullptr, nullptr);
  if (length <= 0)
    return std::string();

  std::string result(static_cast<usize>(length), '\0');
  WideCharToMultiByte(
    CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
    &result[0], length, nullptr, nullptr);
  return result;
}

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

bool IsAbsolutePath(const std::wstring &path) {
  if (path.size() >= 2 && path[1] == L':')
    return true;
  if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    return true;
  return false;
}

std::wstring JoinPath(const std::wstring &left, const std::wstring &right) {
  if (left.empty())
    return right;
  if (right.empty())
    return left;
  if (IsAbsolutePath(right))
    return right;

  wchar_t last = left[left.size() - 1];
  if (last == L'\\' || last == L'/')
    return left + right;
  return left + L"\\" + right;
}

std::wstring ParentPath(const std::wstring &path) {
  usize separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos)
    return std::wstring();
  return path.substr(0, separator);
}

bool EnsureDirectory(const std::wstring &path) {
  if (path.empty())
    return false;

  std::wstring normalized = path;
  for (wchar_t &c : normalized) {
    if (c == L'/')
      c = L'\\';
  }

  usize start = 0;
  if (normalized.size() >= 2 && normalized[1] == L':')
    start = 3;
  else if (normalized.size() >= 2 && normalized[0] == L'\\' &&
           normalized[1] == L'\\')
    start = 2;

  for (usize i = start; i <= normalized.size(); ++i) {
    if (i != normalized.size() && normalized[i] != L'\\')
      continue;
    if (i == 0)
      continue;

    std::wstring part = normalized.substr(0, i);
    if (part.size() == 2 && part[1] == L':')
      continue;

    CreateDirectoryW(part.c_str(), nullptr);
  }

  DWORD attributes = GetFileAttributesW(normalized.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string ReadUtf8File(const std::wstring &path) {
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

bool WriteUtf8File(const std::wstring &path, const std::string &data) {
  FILE *file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file)
    return false;

  bool ok = data.empty() ||
            fwrite(data.data(), 1, data.size(), file) == data.size();
  fclose(file);
  return ok;
}

std::wstring TimestampName() {
  SYSTEMTIME time;
  GetLocalTime(&time);

  wchar_t name[128] = {0};
  swprintf_s(
    name,
    L"light-flow-%04d%02d%02d-%02d%02d%02d.mp4",
    time.wYear,
    time.wMonth,
    time.wDay,
    time.wHour,
    time.wMinute,
    time.wSecond);
  return name;
}

bool __fastcall HookSystemIOIsCapturingMovie(void *pThis) {
  // Only alter the answer while Compositor::Render() itself is asking. The
  // game loop and platform code also poll this function and would otherwise
  // stop presenting frames entirely.
  if (g_forceMovieThisRender)
    return true;

  return Sky::SystemIO::sfn_IsCapturingMovie_0.Call(
    reinterpret_cast<Sky::SystemIO *>(pThis));
}

} // namespace

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void Settings::Load(const std::wstring &path) {
  std::string data = ReadUtf8File(path);
  if (data.empty())
    return;

  rapidjson::Document document;
  if (document.Parse(data.c_str()).HasParseError() || !document.IsObject())
    return;

  if (document.HasMember("output") && document["output"].IsString())
    output = document["output"].GetString();
  if (document.HasMember("codec") && document["codec"].IsString())
    codec = document["codec"].GetString();
  if (document.HasMember("bitrateKbps") && document["bitrateKbps"].IsInt())
    bitrateKbps = std::max(100, document["bitrateKbps"].GetInt());
  if (document.HasMember("targetFps") && document["targetFps"].IsInt())
    targetFps = std::min(240, std::max(0, document["targetFps"].GetInt()));
  if (document.HasMember("queueSize") && document["queueSize"].IsInt())
    queueSize = std::min(16, std::max(1, document["queueSize"].GetInt()));
  if (document.HasMember("forceMovieMode") && document["forceMovieMode"].IsBool())
    forceMovieMode = document["forceMovieMode"].GetBool();
  if (document.HasMember("showOverlay") && document["showOverlay"].IsBool())
    showOverlay = document["showOverlay"].GetBool();
}

void Settings::Save(const std::wstring &path) const {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType &allocator = document.GetAllocator();

  document.AddMember(
    "output", rapidjson::Value(output.c_str(), allocator), allocator);
  document.AddMember(
    "codec", rapidjson::Value(codec.c_str(), allocator), allocator);
  document.AddMember("bitrateKbps", bitrateKbps, allocator);
  document.AddMember("targetFps", targetFps, allocator);
  document.AddMember("queueSize", queueSize, allocator);
  document.AddMember("forceMovieMode", forceMovieMode, allocator);
  document.AddMember("showOverlay", showOverlay, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);

  WriteUtf8File(
    path, std::string(buffer.GetString(), static_cast<usize>(buffer.GetSize())));
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LightFlowMod::LightFlowMod() = default;

LightFlowMod::~LightFlowMod() {
  Shutdown();
}

bool LightFlowMod::Initialize(HMODULE module) {
  if (m_initialized.exchange(true))
    return true;

  m_module = module;
  m_modFolder = GetModuleFolder(module);
  m_configPath = JoinPath(m_modFolder, L"light-flow.json");
  m_capturesFolder = JoinPath(m_modFolder, L"captures");

  VideoEncoder::GlobalInitialize();
  m_settings.Load(m_configPath);
  SyncSettingsToUi();

  SetStatus("Ready. F8 opens the panel, F9 starts/stops recording.");
  return true;
}

void LightFlowMod::Shutdown() {
  if (!m_initialized.exchange(false))
    return;

  StopCapture(true);
  RemoveMovieHook();

  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_settings.Save(m_configPath);
  }

  SetStatus("Stopped.");
}

void LightFlowMod::SyncSettingsToUi() {
  std::lock_guard<std::mutex> lock(m_settingsMutex);

  snprintf(m_outputBuffer, sizeof(m_outputBuffer), "%s", m_settings.output.c_str());
  m_forceMovie = m_settings.forceMovieMode;
  m_showOverlay = m_settings.showOverlay;
  m_targetFps = m_settings.targetFps;
  m_queueLimit = std::max(1, m_settings.queueSize);
}

// ---------------------------------------------------------------------------
// Capture control
// ---------------------------------------------------------------------------

void LightFlowMod::ToggleMenu() {
  m_menuOpen = !m_menuOpen.load();
}

void LightFlowMod::RequestToggleCapture() {
  m_toggleCaptureRequested = true;
}

void LightFlowMod::OnDisable() {
  StopCapture(true);
}

void LightFlowMod::ToggleCapture() {
  if (m_capturing.load())
    StopCapture(false);
  else
    StartCapture();
}

void LightFlowMod::StartCapture() {
  if (m_capturing.exchange(true))
    return;

  std::string error;
  std::string output = ResolveOutputPath(error);
  if (output.empty()) {
    m_capturing = false;
    SetStatus("Cannot start capture: %s", error.c_str());
    HTTellText("[light-flow] Cannot start capture: %s", error.c_str());
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_activeOutput = output;
    m_activeCodec = m_settings.codec;
    m_activeBitrate = std::max(100, m_settings.bitrateKbps);
    m_activeFps = m_settings.targetFps > 0 ? m_settings.targetFps : 30;
    m_forceMovie = m_settings.forceMovieMode;
    m_showOverlay = m_settings.showOverlay;
    m_targetFps = m_settings.targetFps;
    m_queueLimit = std::max(1, m_settings.queueSize);
  }

  // Finish any previous worker before resetting the stop flag, otherwise the
  // old worker could observe m_stopWorker == false and never exit.
  if (m_worker.joinable()) {
    m_stopWorker = true;
    m_queueCondition.notify_all();
    m_worker.join();
  }

  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.clear();
    m_freeBuffers.clear();
  }

  m_stopWorker = false;
  m_workerDone = false;
  m_failed = false;
  m_capturedFrames = 0;
  m_encodedFrames = 0;
  m_droppedFrames = 0;
  m_encoderWidth = 0;
  m_encoderHeight = 0;
  m_lastGrabFailLogMs = 0;
  m_screenshotPendingThisFrame = false;

  m_startTime = std::chrono::steady_clock::now();
  m_lastGrabTime = m_startTime - std::chrono::seconds(1);
  m_worker = std::thread(&LightFlowMod::WorkerLoop, this);

  SetStatus("Starting capture -> %s", output.c_str());
  HTTellText("[light-flow] Starting capture -> %s", output.c_str());
}

void LightFlowMod::StopCapture(bool join) {
  bool wasCapturing = m_capturing.exchange(false);

  m_stopWorker = true;
  m_queueCondition.notify_all();

  if (wasCapturing)
    SetStatus("Stopping capture...");

  if (join) {
    if (m_worker.joinable())
      m_worker.join();
    m_workerDone = true;
  }
}

void LightFlowMod::ReapWorker() {
  if (m_worker.joinable() && m_workerDone.load()) {
    m_worker.join();
    if (!m_capturing.load() && !m_failed.load())
      SetStatus("Stopped. %llu frames encoded.",
                static_cast<unsigned long long>(m_encodedFrames.load()));
  }
}

void LightFlowMod::WorkerLoop() {
  VideoEncoder encoder;
  std::string error;
  bool opened = false;

  std::string output;
  std::string codec;
  i32 bitrate = 12000;
  i32 fps = 30;
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    output = m_activeOutput;
    codec = m_activeCodec;
    bitrate = m_activeBitrate;
    fps = m_activeFps;
  }

  while (true) {
    QueuedFrame frame;
    {
      std::unique_lock<std::mutex> lock(m_queueMutex);
      m_queueCondition.wait(lock, [this] {
        return m_stopWorker.load() || !m_queue.empty();
      });

      if (m_queue.empty()) {
        if (m_stopWorker.load())
          break;
        continue;
      }

      frame = std::move(m_queue.front());
      m_queue.pop_front();
    }

    if (!opened && !m_failed.load()) {
      if (!encoder.Open(
            output,
            frame.width,
            frame.height,
            fps,
            bitrate,
            codec,
            error)) {
        SetStatus("Encoder error: %s", error.c_str());
        m_failed = true;
        m_capturing = false;
        m_stopWorker = true;
      } else {
        opened = true;
        m_encoderWidth = encoder.Width();
        m_encoderHeight = encoder.Height();
        SetStatus(
          "Recording %dx%d with %s -> %s",
          encoder.Width(),
          encoder.Height(),
          encoder.Url().c_str(),
          output.c_str());
      }
    }

    if (opened && !m_failed.load()) {
      if (!encoder.Encode(
            frame.pixels.data(),
            frame.width,
            frame.height,
            frame.format,
            frame.ptsMs,
            error)) {
        SetStatus("Encode error: %s", error.c_str());
        m_failed = true;
        m_capturing = false;
        m_stopWorker = true;
      } else {
        m_encodedFrames++;
      }
    }

    {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_freeBuffers.push_back(std::move(frame.pixels));
    }

    if (m_failed.load()) {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      for (QueuedFrame &queued : m_queue) {
        m_freeBuffers.push_back(std::move(queued.pixels));
      }
      m_queue.clear();
      break;
    }

    if (m_stopWorker.load() && m_queue.empty())
      break;
  }

  encoder.Close();
  m_workerDone = true;

  if (!m_failed.load()) {
    SetStatus(
      "Recording finished. %llu frames encoded.",
      static_cast<unsigned long long>(m_encodedFrames.load()));
  }
}

// ---------------------------------------------------------------------------
// Compositor hook entry points
// ---------------------------------------------------------------------------

void LightFlowMod::OnCompositorPreRender(void *compositor) {
  if (!m_capturing.load() || !compositor)
    return;

  m_grabber.SetCompositor(compositor);

  u08 screenshotPending =
    *(At(compositor, Sky::CompositorOffsets::kScreenshotRequested));
  m_screenshotPendingThisFrame = screenshotPending != 0;

  if (m_forceMovie.load() && !m_movieHookInstalled.load())
    EnsureMovieHook(compositor);
}

void LightFlowMod::OnCompositorRender(
  void *compositor,
  u32 frameIndex,
  bool screenshotPending,
  bool movieMode
) {
  if (!m_capturing.load() || !compositor)
    return;

  // The game took a real screenshot this frame. Its render path replaced the
  // movie capture path, so rtScreenshot does not contain a movie frame.
  if (screenshotPending)
    return;

  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  i32 targetFps = m_targetFps.load();

  if (targetFps > 0) {
    std::chrono::microseconds interval(1000000 / targetFps);
    if (now - m_lastGrabTime < interval)
      return;
  }
  m_lastGrabTime = now;

  std::vector<u08> buffer;
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (!m_freeBuffers.empty()) {
      buffer = std::move(m_freeBuffers.back());
      m_freeBuffers.pop_back();
    }
  }

  i32 width = 0;
  i32 height = 0;
  Sky::SourcePixelFormat pixelFormat = Sky::SourcePixelFormat::Unknown;
  u32 gfxFormat = 0;
  u32 vkFormat = 0;

  if (!m_grabber.Grab(
        frameIndex,
        buffer,
        width,
        height,
        pixelFormat,
        gfxFormat,
        vkFormat)) {
    u64 elapsedMs = static_cast<u64>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_startTime).count());

    if (m_lastGrabFailLogMs == 0 ||
        elapsedMs - m_lastGrabFailLogMs > 2000) {
      m_lastGrabFailLogMs = elapsedMs;
      SetStatus("Waiting for rtScreenshot: %s", m_grabber.LastError().c_str());
    }

    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_freeBuffers.push_back(std::move(buffer));
    return;
  }

  // Tonemap_movie is compiled with the "BGR" define, unlike
  // Tonemap_screenshot. When we forced movie mode, the pixels in
  // rtScreenshot are therefore BGR-ordered regardless of the VkFormat.
  if (movieMode)
    pixelFormat = Sky::SwapRedBlue(pixelFormat);

  {
    static bool loggedFormat = false;
    if (!loggedFormat) {
      loggedFormat = true;
      HTTellText(
        "[light-flow] rtScreenshot: logical=%u VkFormat=%u %dx%d "
        "movie=%d -> source format %d",
        gfxFormat,
        vkFormat,
        width,
        height,
        movieMode ? 1 : 0,
        static_cast<int>(pixelFormat));
    }
  }

  i64 ptsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - m_startTime).count();

  {
    std::lock_guard<std::mutex> lock(m_queueMutex);

    if (m_failed.load() ||
        static_cast<i32>(m_queue.size()) >= m_queueLimit.load()) {
      m_droppedFrames++;
      m_freeBuffers.push_back(std::move(buffer));
      return;
    }

    QueuedFrame frame;
    frame.pixels = std::move(buffer);
    frame.width = width;
    frame.height = height;
    frame.format = pixelFormat;
    frame.ptsMs = ptsMs;

    m_queue.push_back(std::move(frame));
    m_capturedFrames++;
  }

  m_queueCondition.notify_one();
}

bool LightFlowMod::ShouldForceMovie() const {
  return m_capturing.load(std::memory_order_relaxed) &&
         m_forceMovie.load(std::memory_order_relaxed) &&
         !m_screenshotPendingThisFrame.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// SystemIO::IsCapturingMovie hook
// ---------------------------------------------------------------------------

bool LightFlowMod::EnsureMovieHook(void *compositor) {
  if (m_movieHookInstalled.load())
    return true;

  // Give the compositor a couple of seconds to finish initializing before
  // telling the user that automatic movie mode could not be enabled.
  if (!m_movieHookWarned && ++m_movieHookAttempts == 120) {
    m_movieHookWarned = true;
    HTTellText(
      "[light-flow] SystemIO::IsCapturingMovie was not found. "
      "If rtScreenshot stays black, this game build may need updated offsets.");
  }

  std::lock_guard<std::mutex> lock(m_movieHookMutex);
  if (m_movieHookInstalled.load())
    return true;

  void *systemIO = Sky::FrameGrabber::GetSystemIO(compositor);
  if (!systemIO)
    return false;

  auto &vf = Sky::SystemIO::sfn_IsCapturingMovie_0;

  if (!vf.Function().IsValid()) {
    // The detour must be set before Cache() assigns the address.
    vf.Function().Redirect(
      reinterpret_cast<BinaryFunctionBase::FuncPtr>(
        &HookSystemIOIsCapturingMovie));
    vf.Cache(reinterpret_cast<Sky::SystemIO *>(systemIO));
  }

  if (!vf.Function().IsHookEnabled())
    vf.Hook();

  m_movieHookInstalled = true;

  HTTellText(
    "[light-flow] Hooked SystemIO::IsCapturingMovie at %p",
    (void *)vf.Function().Address());
  return true;
}

void LightFlowMod::RemoveMovieHook() {
  std::lock_guard<std::mutex> lock(m_movieHookMutex);

  if (!m_movieHookInstalled.exchange(false))
    return;

  auto &vf = Sky::SystemIO::sfn_IsCapturingMovie_0;

  if (vf.Function().IsHookEnabled())
    vf.Unhook();
}

// ---------------------------------------------------------------------------
// Output paths
// ---------------------------------------------------------------------------

std::string LightFlowMod::ResolveOutputPath(std::string &error) {
  std::string configured;
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    configured = m_settings.output;
  }

  if (!configured.empty()) {
    if (configured.find("://") != std::string::npos)
      return configured;

    std::wstring path = Utf8ToWide(configured);
    if (path.empty()) {
      error = "output path is not valid UTF-8";
      return std::string();
    }

    if (!IsAbsolutePath(path))
      path = JoinPath(m_modFolder, path);

    std::wstring parent = ParentPath(path);
    if (!parent.empty() && !EnsureDirectory(parent)) {
      error = "cannot create output directory: " + WideToUtf8(parent);
      return std::string();
    }

    return WideToUtf8(path);
  }

  if (!EnsureDirectory(m_capturesFolder)) {
    error = "cannot create captures directory: " +
            WideToUtf8(m_capturesFolder);
    return std::string();
  }

  std::wstring path = JoinPath(m_capturesFolder, TimestampName());
  return WideToUtf8(path);
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void LightFlowMod::SetStatus(const char *format, ...) {
  char buffer[512];
  va_list args;

  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  std::lock_guard<std::mutex> lock(m_statusMutex);
  m_status = buffer;
}

std::string LightFlowMod::GetStatus() {
  std::lock_guard<std::mutex> lock(m_statusMutex);
  return m_status;
}

void LightFlowMod::BrowseOutput() {
  wchar_t file[1024] = {0};
  std::string current(m_outputBuffer);

  if (current.find("://") == std::string::npos && !current.empty()) {
    std::wstring wide = Utf8ToWide(current);
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

  EnsureDirectory(m_capturesFolder);
  dialog.lpstrInitialDir = m_capturesFolder.c_str();

  if (!GetSaveFileNameW(&dialog))
    return;

  std::string output = WideToUtf8(file);
  snprintf(m_outputBuffer, sizeof(m_outputBuffer), "%s", output.c_str());

  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.output = output;
}

void LightFlowMod::OpenCapturesFolder() {
  EnsureDirectory(m_capturesFolder);
  ShellExecuteW(
    nullptr,
    L"open",
    m_capturesFolder.c_str(),
    nullptr,
    nullptr,
    SW_SHOWNORMAL);
}

void LightFlowMod::DrawOverlay() {
  if (!m_capturing.load() || !m_showOverlay.load())
    return;

  u64 captured = m_capturedFrames.load();
  u64 dropped = m_droppedFrames.load();

  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  i64 seconds = std::chrono::duration_cast<std::chrono::seconds>(
    now - m_startTime).count();

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

void LightFlowMod::DrawPanel() {
  static const char *kCodecs[] = {
    "libx264", "h264_mf", "libopenh264", "mpeg4"
  };

  bool open = m_menuOpen.load();

  ImGui::PushID("light-flow-panel");
  ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Light Flow", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
      "Captures the game's rtScreenshot render target and encodes it with "
      "FFmpeg. Press F9 to start or stop recording.");
    ImGui::Separator();

    ImGui::SetNextItemWidth(-220.0f);
    ImGui::InputText("##output", m_outputBuffer, sizeof(m_outputBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
      BrowseOutput();
    ImGui::SameLine();
    if (ImGui::Button("Folder"))
      OpenCapturesFolder();

    ImGui::TextDisabled(
      "Empty = <mod folder>\\captures\\light-flow-<time>.mp4. "
      "rtmp://, srt://, udp:// URLs are written directly.");

    ImGui::Spacing();

    std::string codec;
    i32 bitrate = 12000;
    i32 fps = 30;
    i32 queueSize = 4;
    {
      std::lock_guard<std::mutex> lock(m_settingsMutex);
      codec = m_settings.codec;
      bitrate = m_settings.bitrateKbps;
      fps = m_settings.targetFps;
      queueSize = m_settings.queueSize;
    }

    int codecIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kCodecs); ++i) {
      if (codec == kCodecs[i]) {
        codecIndex = i;
        break;
      }
    }

    if (ImGui::Combo("Codec", &codecIndex, kCodecs, IM_ARRAYSIZE(kCodecs))) {
      std::lock_guard<std::mutex> lock(m_settingsMutex);
      m_settings.codec = kCodecs[codecIndex];
    }

    if (ImGui::InputInt("Bitrate (kbps)", &bitrate)) {
      bitrate = std::max(100, bitrate);
      std::lock_guard<std::mutex> lock(m_settingsMutex);
      m_settings.bitrateKbps = bitrate;
    }

    if (ImGui::InputInt("Capture FPS (0 = every frame)", &fps)) {
      fps = std::min(240, std::max(0, fps));
      m_targetFps = fps;
      std::lock_guard<std::mutex> lock(m_settingsMutex);
      m_settings.targetFps = fps;
    }

    if (ImGui::SliderInt("Encoder queue", &queueSize, 1, 16)) {
      queueSize = std::min(16, std::max(1, queueSize));
      m_queueLimit = queueSize;
      std::lock_guard<std::mutex> lock(m_settingsMutex);
      m_settings.queueSize = queueSize;
    }

    bool forceMovie = m_forceMovie.load();
    if (ImGui::Checkbox("Force movie capture mode", &forceMovie)) {
      m_forceMovie = forceMovie;
      std::lock_guard<std::mutex> lock(m_settingsMutex);
      m_settings.forceMovieMode = forceMovie;
    }

    ImGui::SameLine();

    bool showOverlay = m_showOverlay.load();
    if (ImGui::Checkbox("REC overlay", &showOverlay)) {
      m_showOverlay = showOverlay;
      std::lock_guard<std::mutex> lock(m_settingsMutex);
      m_settings.showOverlay = showOverlay;
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button(
          m_capturing.load() ? "Stop recording (F9)" : "Start recording (F9)",
          ImVec2(220.0f, 0.0f)))
      ToggleCapture();

    ImGui::SameLine();

    if (ImGui::Button("Save settings")) {
      std::lock_guard<std::mutex> lock(m_settingsMutex);
      m_settings.Save(m_configPath);
      SetStatus("Settings saved.");
    }

    ImGui::Separator();

    if (m_capturing.load()) {
      i64 seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_startTime).count();

      int queued = 0;
      {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        queued = static_cast<int>(m_queue.size());
      }

      ImGui::Text(
        "State: recording%s",
        m_failed.load() ? " (error)" : "");
      ImGui::Text(
        "Resolution: %dx%d",
        m_encoderWidth.load(),
        m_encoderHeight.load());
      ImGui::Text(
        "Captured: %llu   Encoded: %llu   Dropped: %llu",
        static_cast<unsigned long long>(m_capturedFrames.load()),
        static_cast<unsigned long long>(m_encodedFrames.load()),
        static_cast<unsigned long long>(m_droppedFrames.load()));
      ImGui::Text("Queue: %d / %d", queued, m_queueLimit.load());
      ImGui::Text(
        "Elapsed: %02lld:%02lld",
        static_cast<long long>(seconds / 60),
        static_cast<long long>(seconds % 60));
    } else {
      ImGui::TextUnformatted("State: idle");
    }

    std::string status = GetStatus();
    ImGui::TextWrapped("%s", status.c_str());
  }

  ImGui::End();
  ImGui::PopID();

  m_menuOpen = open;
}

void LightFlowMod::OnGui(float /*deltaTime*/) {
  ReapWorker();

  if (m_toggleCaptureRequested.exchange(false))
    ToggleCapture();

  // This mod compiles its own ImGui, while the loader owns the ImGui context
  // and the rendering backend. Adopt the loader's context on the first frame.
  if (!m_imguiReady) {
    HTImGuiContexts context = {0};
    if (HTImGuiDispatch(&context) != HT_SUCCESS || !context.pImGui)
      return;

    ImGui::SetCurrentContext(
      reinterpret_cast<ImGuiContext *>(context.pImGui));

    if (context.pImAllocatorAllocFunc && context.pImAllocatorFreeFunc) {
      ImGui::SetAllocatorFunctions(
        reinterpret_cast<ImGuiMemAllocFunc>(context.pImAllocatorAllocFunc),
        reinterpret_cast<ImGuiMemFreeFunc>(context.pImAllocatorFreeFunc),
        context.pImAllocatorUserData);
    }

    m_imguiReady = true;
  }

  DrawOverlay();

  if (m_menuOpen.load())
    DrawPanel();
}

} // namespace LightFlow
