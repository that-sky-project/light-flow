
#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "utils/logger.hpp"
#include "utils/pathUtils.hpp"
#include "utils/fsUtils.hpp"
#include "utils/utf8Utils.hpp"
#include "utils/winapicall.hpp"
#include "skylink/skyRenderer.hpp"
#include "skylink/skySystemIO.hpp"
#include "skylink/skyTexture.hpp"
#include "record/modui.hpp"
#include "record/hook.hpp"
#include "record/lightflow.hpp"

static LightFlowMod *g_lightFlow = nullptr;

namespace {

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

void __fastcall HotkeyToggleMenu(HTKeyEvent *event) {
  if ((event->flags & HTKeyEventFlags_Mask) != HTKeyEventFlags_Down)
    return;
  if (g_lightFlow)
    g_lightFlow->ToggleMenu();
}

void __fastcall HotkeyToggleCapture(HTKeyEvent *event) {
  if ((event->flags & HTKeyEventFlags_Mask) != HTKeyEventFlags_Down)
    return;
  if (g_lightFlow)
    g_lightFlow->RequestToggleCapture();
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LightFlowMod::~LightFlowMod() {
  Shutdown();
}

bool LightFlowMod::Initialize(HMODULE module, GameProfile profile) {
  if (m_initialized)
    return true;
  m_initialized = true;
  g_lightFlow = this;

  m_module = module;
  m_profile = profile;
  m_compositorLayout = &GetCompositorLayout(profile);

  m_modFolder = Win64SysCall::GetModuleFolder(module);
  m_capturesFolder = PathUtils::Join({m_modFolder, L"captures"});

  VideoEncoder::GlobalInitialize();
  m_settings.Load();

  m_lightFlowUi = new LightFlowUi();
  m_lightFlowUi->Initialize();
  SyncSettingsToUi();

  m_hotkeyMenu = HTHotkeyRegisterEx(
    module, "Toggle Light Flow panel", HTKey_F8, HTHotkeyFlags_NoBlock);
  m_hotkeyCapture = HTHotkeyRegisterEx(
    module, "Start/stop Light Flow recording", HTKey_F9,
    HTHotkeyFlags_NoBlock);

  if (m_hotkeyMenu != HT_INVALID_HANDLE)
    HTHotkeyListen(m_hotkeyMenu, HotkeyToggleMenu);
  else
    LOGW("Cannot register the F8 hotkey");

  if (m_hotkeyCapture != HT_INVALID_HANDLE)
    HTHotkeyListen(m_hotkeyCapture, HotkeyToggleCapture);
  else
    LOGW("Cannot register the F9 hotkey");

  LOG("LightFlowMod initialized (%s edition).", GetGameProfileName(profile));
  LOG("Mod folder: %s", Utf8Utils::ToUtf8(m_modFolder).c_str());

  SetStatus("Ready. F8 opens the panel, F9 starts/stops recording.");
  return true;
}

void LightFlowMod::Shutdown() {
  if (!m_initialized)
    return;
  m_initialized = false;

  StopCapture(true);

  if (m_hotkeyMenu != HT_INVALID_HANDLE) {
    HTHotkeyUnlisten(m_hotkeyMenu, nullptr);
    m_hotkeyMenu = HT_INVALID_HANDLE;
  }
  if (m_hotkeyCapture != HT_INVALID_HANDLE) {
    HTHotkeyUnlisten(m_hotkeyCapture, nullptr);
    m_hotkeyCapture = HT_INVALID_HANDLE;
  }

  m_settings.Save();

  // The UI object is intentionally left alive: the mod is never unloaded and a
  // late HTModRenderGui() call must not touch freed memory.
  SetStatus("Stopped.");
}

void LightFlowMod::MarkSettingsDirty() {
  m_settingsDirty = true;
  m_settingsDirtyTime = std::chrono::steady_clock::now();
}

void LightFlowMod::SyncSettingsToUi() {
  m_showOverlay = m_settings.showOverlay;
  m_swapRedBlue = m_settings.swapRedBlue;
  m_targetFps = m_settings.targetFps;
  m_queueLimit = std::max(1, m_settings.queueSize);
}

void LightFlowMod::ToggleMenu() {
  if (m_lightFlowUi)
    m_lightFlowUi->ToggleMenu();
}

bool LightFlowMod::IsMenuOpen() const {
  return m_lightFlowUi && m_lightFlowUi->IsMenuOpen();
}

void LightFlowMod::RequestToggleCapture() {
  m_toggleCaptureRequested = true;
}

void LightFlowMod::OnDisable() {
  // Stop the session but leave the SystemIO detours installed: they pass the
  // original value through while g_forceMovieThisRender is false, and keeping
  // them avoids creating a second MinHook on the same function when the mod is
  // enabled again.
  StopCapture(true);
}

// ---------------------------------------------------------------------------
// Capture control
// ---------------------------------------------------------------------------

void LightFlowMod::Update() {
  ReapWorker();

  if (m_settingsDirty &&
      std::chrono::steady_clock::now() - m_settingsDirtyTime >=
        std::chrono::milliseconds(500)) {
    m_settings.Save();
    m_settingsDirty = false;
  }

  if (m_coreDisabled) {
    // The mod shut its main feature down after a failed hook. Keep the flag
    // clear so a queued hotkey press cannot start a broken session.
    m_toggleCaptureRequested = false;
    return;
  }

  if (m_toggleCaptureRequested) {
    m_toggleCaptureRequested = false;
    ToggleCapture();
  }
}

void LightFlowMod::ToggleCapture() {
  if (m_coreDisabled)
    return;

  if (m_capturing)
    StopCapture(false);
  else
    StartCapture();
}

void LightFlowMod::StartCapture() {
  if (m_coreDisabled)
    return;

  if (m_capturing)
    return;
  m_capturing = true;

  std::string error;
  std::string output = ResolveOutputPath(error);
  if (output.empty()) {
    m_capturing = false;
    SetStatus("Cannot start capture: %s", error.c_str());
    LOGW("Cannot start capture: %s", error.c_str());
    return;
  }

  m_activeOutput = output;
  m_activeCodec = m_settings.codec;
  m_activeBitrate = std::max(100, m_settings.bitrateKbps);
  m_activeFps = m_settings.targetFps > 0 ? m_settings.targetFps : 30;
  m_showOverlay = m_settings.showOverlay;
  m_swapRedBlue = m_settings.swapRedBlue;
  m_targetFps = m_settings.targetFps;
  m_queueLimit = std::max(1, m_settings.queueSize);

  // Finish any previous workers before resetting the stop flags, otherwise an
  // old worker could observe m_stopWorker == false and never exit.
  if (m_grabWorker.joinable() || m_worker.joinable()) {
    {
      std::lock_guard<std::mutex> lock(m_grabMutex);
      m_stopGrabber = true;
    }
    {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_stopWorker = true;
    }
    m_grabCondition.notify_all();
    m_queueCondition.notify_all();
    if (m_grabWorker.joinable())
      m_grabWorker.join();
    if (m_worker.joinable())
      m_worker.join();
  }

  {
    std::lock_guard<std::mutex> lock(m_grabMutex);
    m_grabPending = false;
  }

  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.clear();
    m_freeBuffers.clear();
  }

  m_stopGrabber = false;
  m_grabWorkerDone = false;
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
  m_grabWorker = std::thread(&LightFlowMod::GrabberLoop, this);

  SetStatus("Starting capture -> %s", output.c_str());
  LOG("Starting capture -> %s", output.c_str());
}

void LightFlowMod::StopCapture(bool join) {
  bool wasCapturing = m_capturing;
  m_capturing = false;

  // The stop flags are part of the workers' wait predicates, so they must be
  // stored under the matching mutex; otherwise a worker that is between its
  // predicate check and its wait could miss the notification and hang.
  {
    std::lock_guard<std::mutex> lock(m_grabMutex);
    m_stopGrabber = true;
  }
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_stopWorker = true;
  }
  m_grabCondition.notify_all();
  m_queueCondition.notify_all();

  if (wasCapturing)
    SetStatus("Stopping capture...");

  if (join) {
    // Join the grab worker first so no more frames can enter the queue while
    // the encoder drains the remaining ones.
    if (m_grabWorker.joinable())
      m_grabWorker.join();
    m_grabWorkerDone = true;

    if (m_worker.joinable())
      m_worker.join();
    m_workerDone = true;
  }
}

void LightFlowMod::ReapWorker() {
  if (m_grabWorker.joinable() && m_grabWorkerDone.load())
    m_grabWorker.join();

  if (m_worker.joinable() && m_workerDone.load()) {
    m_worker.join();
    if (m_failed.load()) {
      // The worker stops on an encoder error without touching m_capturing,
      // which belongs to the game thread.
      m_capturing = false;
    } else if (!m_capturing) {
      SetStatus(
        "Stopped. %llu frames encoded.",
        static_cast<unsigned long long>(m_encodedFrames.load()));
    }
  }
}

// Runs on the render thread. Only the request is recorded here: the actual
// rtScreenshot read (a GPU synchronise plus a full-frame copy) is performed by
// GrabberLoop() so this thread can carry on with the next frame.
void LightFlowMod::RequestGrab(u32 frameIndex, i64 ptsMs, bool swapRedBlue) {
  {
    std::lock_guard<std::mutex> lock(m_grabMutex);
    // The worker cannot keep up with this frame; replacing the stale request
    // is safer than reading a texture slot the game may already have reused.
    if (m_grabPending)
      m_droppedFrames++;
    m_grabRequest.frameIndex = frameIndex;
    m_grabRequest.ptsMs = ptsMs;
    m_grabRequest.swapRedBlue = swapRedBlue;
    m_grabPending = true;
  }

  m_grabCondition.notify_one();
}

void LightFlowMod::GrabberLoop() {
  // FrameGrabber calls the hooked FindRenderTarget() from this thread. Record
  // the id so the hook can tell those calls apart from the game's own.
  m_grabThreadId = std::this_thread::get_id();

  while (true) {
    GrabRequest request;
    {
      std::unique_lock<std::mutex> lock(m_grabMutex);
      m_grabCondition.wait(lock, [this] {
        return m_stopGrabber.load() || m_grabPending;
      });

      if (m_stopGrabber.load())
        break;

      request = m_grabRequest;
      m_grabPending = false;
    }

    GrabFrame(request);
  }

  // Let the encoder worker know that no further frames will be queued. The
  // flag is part of its wait predicate, so store it under the queue mutex.
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_grabWorkerDone = true;
  }
  m_queueCondition.notify_all();
}

void LightFlowMod::GrabFrame(const GrabRequest &request) {
  if (m_failed.load())
    return;

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
  SourcePixelFormat pixelFormat = kSourcePixelFormat_Unknown;
  GfxTexelFormat gfxFormat = kGfxTexelFormat_Undefined;

  if (!m_grabber.Grab(
    request.frameIndex,
    &buffer,
    &width,
    &height,
    &pixelFormat,
    &gfxFormat
  )) {
    u64 elapsedMs = static_cast<u64>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_startTime).count());

    if (m_lastGrabFailLogMs == 0 ||
        elapsedMs - m_lastGrabFailLogMs > 2000) {
      m_lastGrabFailLogMs = elapsedMs;
      SetStatus("Waiting for rtScreenshot: %s", m_grabber.LastError().c_str());
    }

    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_freeBuffers.push_back(std::move(buffer));
    return;
  }

  // Optional user correction for builds/shaders that store the red and blue
  // channels the other way around.
  if (request.swapRedBlue)
    pixelFormat = SwapRedBlue(pixelFormat);

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
    frame.ptsMs = request.ptsMs;

    m_queue.push_back(std::move(frame));
    m_capturedFrames++;
  }

  m_queueCondition.notify_one();
}

void LightFlowMod::WorkerLoop() {
  VideoEncoder encoder;
  std::string error;
  bool opened = false;

  std::string output;
  std::string codec;
  i32 bitrate = 12000;
  i32 fps = 30;
  // StartCapture() wrote these before creating this thread.
  output = m_activeOutput;
  codec = m_activeCodec;
  bitrate = m_activeBitrate;
  fps = m_activeFps;

  while (true) {
    QueuedFrame frame;
    {
      std::unique_lock<std::mutex> lock(m_queueMutex);
      m_queueCondition.wait(lock, [this] {
        // Drain everything the grab worker managed to queue before exiting on
        // a normal stop.
        return !m_queue.empty() ||
          (m_stopWorker.load() && m_grabWorkerDone.load());
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
        LOGE("Encoder error: %s", error.c_str());
        m_failed = true;
        m_stopWorker = true;
      } else {
        opened = true;
        m_encoderWidth = encoder.Width();
        m_encoderHeight = encoder.Height();
        SetStatus(
          "Recording %dx%d -> %s",
          encoder.Width(),
          encoder.Height(),
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
        LOGE("Encode error: %s", error.c_str());
        m_failed = true;
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
      // Stop the grab worker as well; it may be blocked on a pending request.
      {
        std::lock_guard<std::mutex> lock(m_grabMutex);
        m_stopGrabber = true;
      }
      m_grabCondition.notify_all();

      std::lock_guard<std::mutex> lock(m_queueMutex);
      for (QueuedFrame &queued : m_queue) {
        m_freeBuffers.push_back(std::move(queued.pixels));
      }
      m_queue.clear();
      break;
    }

    if (m_stopWorker.load() && m_grabWorkerDone.load() && m_queue.empty())
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

void LightFlowMod::OnCompositorPreRender(Compositor *compositor) {
  if (!compositor || !m_capturing || m_coreDisabled)
    return;

  const u08 *base = reinterpret_cast<const u08 *>(compositor);
  m_frameIndex = *reinterpret_cast<const u32 *>(
    base + m_compositorLayout->frameCounter);
  m_screenshotPendingThisFrame = compositor->*Compositor::screenshotRequested;
}

void LightFlowMod::OnRenderTargetsFound(
  const RenderOutline *renderOutline,
  const RenderTarget *targets
) {
  // The grab worker reaches this hook too: FrameGrabber calls FindRenderTarget
  // to walk the table it already cached. Only the game's own call carries a
  // fresh table, and letting the worker write its (possibly stale) copy back
  // would race with the game's resize-time updates.
  if (std::this_thread::get_id() == m_grabThreadId.load())
    return;

  m_grabber.SetRenderTargets(renderOutline, targets);
}

void LightFlowMod::OnCompositorRender(
  Compositor *compositor,
  bool movieMode
) {
  if (!m_capturing || !compositor || m_coreDisabled)
    return;

  // The game took a real screenshot this frame. Its render path replaced the
  // movie capture path, so rtScreenshot does not contain a movie frame.
  if (m_screenshotPendingThisFrame)
    return;

  // Movie mode was requested but the SystemIO hook is not installed yet;
  // rtScreenshot would still hold a normal (or stale) frame this frame.
  if (!movieMode)
    return;

  // The compositor records the path it actually took in `outputMode` (1 =
  // normal, 2 = movie). Prefer that over our request, which may differ when the
  // game started a movie capture on its own.
  const u08 *compositorBase = reinterpret_cast<const u08 *>(compositor);
  u32 outputMode = *reinterpret_cast<const u32 *>(
    compositorBase + m_compositorLayout->outputMode);
  movieMode = outputMode == 2;

  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  i32 targetFps = m_targetFps;

  if (targetFps > 0) {
    std::chrono::microseconds interval(1000000 / targetFps);
    if (now - m_lastGrabTime < interval)
      return;
  }
  m_lastGrabTime = now;

  i64 ptsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - m_startTime).count();

  // Hand the readback to the grab worker. FrameGrabber touches the renderer
  // and the GPU, which can take milliseconds; doing it here would stall the
  // render thread.
  RequestGrab(m_frameIndex, ptsMs, m_swapRedBlue);
}

bool LightFlowMod::ShouldForceMovie() const {
  return !m_coreDisabled && m_capturing && !m_screenshotPendingThisFrame;
}

// ---------------------------------------------------------------------------
// SystemIO::IsCapturingMovie hooks
// ---------------------------------------------------------------------------

void LightFlowMod::DisableCore(cstring reason) {
  if (m_coreDisabled)
    return;
  m_coreDisabled = true;

  LOGE("%s", reason);
  SetStatus("Disabled: %s", reason);

  // Stop the capture without joining the helper threads from the render
  // thread; ReapWorker() will join them on the next UI frame.
  if (m_capturing) {
    m_capturing = false;
    {
      std::lock_guard<std::mutex> lock(m_grabMutex);
      m_stopGrabber = true;
    }
    {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_stopWorker = true;
    }
    m_grabCondition.notify_all();
    m_queueCondition.notify_all();
  }
}

// ---------------------------------------------------------------------------
// Output paths
// ---------------------------------------------------------------------------

std::string LightFlowMod::ResolveOutputPath(std::string &error) {
  std::string configured = m_settings.output;

  if (!configured.empty()) {
    if (configured.find("://") != std::string::npos)
      return configured;

    std::wstring path = Utf8Utils::ToWide(configured);
    if (path.empty()) {
      error = "output path is not valid UTF-8";
      return std::string();
    }

    if (!PathUtils::IsAbsolute(path))
      path = PathUtils::Join({m_modFolder, path});

    std::wstring parent = PathUtils::ParentPath(path);
    if (!parent.empty() && !FsUtils::EnsureDirectory(parent)) {
      error = "cannot create output directory: " + Utf8Utils::ToUtf8(parent);
      return std::string();
    }

    return Utf8Utils::ToUtf8(path);
  }

  if (!FsUtils::EnsureDirectory(m_capturesFolder)) {
    error = "cannot create captures directory: " +
            Utf8Utils::ToUtf8(m_capturesFolder);
    return std::string();
  }

  std::wstring path = PathUtils::Join({m_capturesFolder, TimestampName()});
  return Utf8Utils::ToUtf8(path);
}

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

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

LightFlowMod *GetLightFlowMod() {
  return g_lightFlow;
}
