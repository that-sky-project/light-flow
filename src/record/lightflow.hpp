#ifndef __RECORD_LIGHTFLOW_HPP__
#define __RECORD_LIGHTFLOW_HPP__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>
#include <htmodloader/htmodloader.h>

#include "utils/types.hpp"
#include "skylink/skyCompositor.hpp"
#include "record/modSettings.hpp"
#include "record/modui.hpp"
#include "record/audio/audioCapturer.hpp"
#include "record/video/frameGrabber.hpp"

// The two Sky releases supported by this mod. The value comes from
// HTGetGameStatus().edition (see mod.cpp).
enum class GameProfile : u08 {
  kNetEase = 0,
  kTgc = 1,
};

inline cstring GetGameProfileName(GameProfile profile) {
  return profile == GameProfile::kNetEase ? "netease" : "tgc";
}

inline const CompositorLayout &GetCompositorLayout(GameProfile profile) {
  return profile == GameProfile::kNetEase
    ? kCompositorLayoutNetease
    : kCompositorLayoutTgc;
}

// ----------------------------------------------------------------------------
// [SECTION] LightFlowMod
// ----------------------------------------------------------------------------

// Threading model: the game core is single threaded. HTModRenderGui(), the
// hotkey callbacks and the Compositor::Render hook all run on that one thread,
// so the state they share needs no atomics. Three helper threads run beside
// it: the grab worker, which reads rtScreenshot so the GPU readback does not
// stall rendering; the audio capture worker, which drains the WASAPI
// process-loopback client; and the encoder worker. The grab and audio workers
// hand frames to the encoder through m_queue / m_audioQueue; the state the
// threads touch is atomic or guarded by m_queueMutex / m_grabMutex.
class LightFlowMod {
public:
  LightFlowMod() = default;
  ~LightFlowMod();

  LightFlowMod(const LightFlowMod &) = delete;
  LightFlowMod(LightFlowMod &&) = delete;
  LightFlowMod &operator=(const LightFlowMod &) = delete;

  bool Initialize(HMODULE module, GameProfile profile);
  void Shutdown();

  // Called by the Compositor::Render hook.
  void OnCompositorPreRender(Compositor *compositor);
  void OnCompositorRender(Compositor *compositor, bool movieMode);

  // Called by the FindRenderTarget() hook once it has resolved "rtScreenshot".
  void OnRenderTargetsFound(const RenderOutline *barn, const RenderTarget *targets);

  // Called from HTModRenderGui().
  void BuildScene();

  // Hotkey handlers.
  void ToggleMenu();
  void RequestToggleCapture();
  void OnDisable();

  bool ShouldForceMovie() const;

  bool IsMenuOpen() const;
  bool IsInitialized() const { return m_initialized; }
  bool IsCapturing() const { return m_capturing; }
  bool IsCoreDisabled() const { return m_coreDisabled; }

  GameProfile Profile() const { return m_profile; }

private:
  friend class LightFlowUi;

  struct QueuedFrame {
    std::vector<u08> pixels;
    i32 width = 0;
    i32 height = 0;
    SourcePixelFormat format = kSourcePixelFormat_Unknown;
    i64 ptsMs = 0;
  };

  // One block of interleaved 16-bit PCM from the process-loopback client.
  // `ptsMs` uses the same clock as QueuedFrame::ptsMs.
  struct QueuedAudio {
    std::vector<i16> samples;
    i32 frames = 0;
    i64 ptsMs = 0;
  };

  // What the render thread asks the grab worker to read. Only one request is
  // kept: if the worker is still busy, the newer frame replaces the stale one
  // (an old frame index may point at a texture slot the game is about to
  // reuse).
  struct GrabRequest {
    u32 frameIndex = 0;
    i64 ptsMs = 0;
    bool swapRedBlue = false;
  };

  void Update();
  void StartCapture();
  void StopCapture(bool join);
  void ToggleCapture();
  void RequestGrab(u32 frameIndex, i64 ptsMs, bool swapRedBlue);
  void GrabberLoop();
  void GrabFrame(const GrabRequest &request);

  void StartAudioSession();
  void OnAudioSamples(const i16 *samples, i32 frames, i64 ptsMs);
  void OnAudioWorkerExit();

  void WorkerLoop();
  void ReapWorker();

  void DisableCore(cstring reason);

  std::string ResolveOutputPath(std::string &error);
  void SetStatus(const char *format, ...);
  std::string GetStatus();

  // Mark the settings for automatic persistence. The UI calls this after every
  // change; Update() flushes them once the user stops editing.
  void MarkSettingsDirty();

  void SyncSettingsToUi();

  bool m_initialized = false;
  HMODULE m_module = nullptr;
  GameProfile m_profile = GameProfile::kTgc;
  const CompositorLayout *m_compositorLayout = &kCompositorLayoutTgc;

  std::wstring m_modFolder;
  std::wstring m_capturesFolder;

  Settings m_settings;

  LightFlowUi *m_lightFlowUi = nullptr;

  FrameGrabber m_grabber;
  bool m_capturing = false;
  bool m_showOverlay = true;
  bool m_swapRedBlue = false;
  i32 m_targetFps = 30;
  std::atomic<i32> m_queueLimit{4};
  bool m_screenshotPendingThisFrame = false;
  u32 m_frameIndex = 0;

  // Capture session. Three helper threads run beside the game thread: the grab
  // worker, the audio capture worker and the encoder worker. Everything they
  // share is atomic or guarded by m_queueMutex / m_grabMutex.
  std::atomic<std::thread::id> m_grabThreadId{};
  std::thread m_grabWorker;
  std::mutex m_grabMutex;
  std::condition_variable m_grabCondition;
  GrabRequest m_grabRequest;
  bool m_grabPending = false;
  std::atomic<bool> m_stopGrabber{false};
  std::atomic<bool> m_grabWorkerDone{true};

  AudioCapturer m_audioCapture;
  std::atomic<bool> m_audioEnabled{false};
  i32 m_activeAudioBitrate = 128;
  i64 m_sessionStartQpc = 0;
  std::deque<QueuedAudio> m_audioQueue;
  std::vector<std::vector<i16>> m_audioFreeBuffers;
  std::atomic<bool> m_audioWorkerDone{true};

  std::thread m_worker;
  std::mutex m_queueMutex;
  std::condition_variable m_queueCondition;
  std::deque<QueuedFrame> m_queue;
  std::vector<std::vector<u08>> m_freeBuffers;
  std::atomic<bool> m_stopWorker{false};
  std::atomic<bool> m_workerDone{true};
  std::atomic<bool> m_failed{false};
  std::atomic<u64> m_encodedFrames{0};
  std::atomic<i32> m_encoderWidth{0};
  std::atomic<i32> m_encoderHeight{0};
  std::atomic<u64> m_capturedFrames{0};
  std::atomic<u64> m_droppedFrames{0};
  std::atomic<u64> m_audioDroppedBlocks{0};

  std::string m_activeOutput;
  std::string m_activeCodec;
  i32 m_activeBitrate = 12000;
  i32 m_activeFps = 30;

  std::chrono::steady_clock::time_point m_startTime;
  std::chrono::steady_clock::time_point m_lastGrabTime;
  u64 m_lastGrabFailLogMs = 0;

  // Settings are written back once editing has been idle for a moment.
  bool m_settingsDirty = false;
  std::chrono::steady_clock::time_point m_settingsDirtyTime;

  // Set when a required hook cannot be installed. The mod then stops
  // recording and refuses to start again.
  bool m_coreDisabled = false;

  // Hotkeys.
  HTHandle m_hotkeyMenu = HT_INVALID_HANDLE;
  HTHandle m_hotkeyCapture = HT_INVALID_HANDLE;

  bool m_toggleCaptureRequested = false;

  mutable std::mutex m_statusMutex;
  std::string m_status = "Idle.";
};

extern LightFlowMod *g_lightFlow;

LightFlowMod *GetLightFlowMod();

#endif
