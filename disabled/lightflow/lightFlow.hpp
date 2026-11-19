#ifndef __LIGHTFLOW_LIGHTFLOW_HPP__
#define __LIGHTFLOW_LIGHTFLOW_HPP__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

#include "utils/types.hpp"
#include "../skylink/skyCompositor.hpp"
#include "../encode/videoEncoder.hpp"

namespace LightFlow {

struct Settings {
  // Empty means "auto": <mod folder>\captures\light-flow-<timestamp>.mp4.
  // A value containing "://" is used as a network URL (rtmp://, srt://, ...).
  std::string output;
  std::string codec = "libx264";
  i32 bitrateKbps = 12000;
  i32 targetFps = 30;
  i32 queueSize = 4;
  bool forceMovieMode = true;
  bool showOverlay = true;

  void Load(const std::wstring &path);
  void Save(const std::wstring &path) const;
};

class LightFlowMod {
public:
  LightFlowMod();
  ~LightFlowMod();

  bool Initialize(HMODULE module);
  void Shutdown();

  // Called by the Compositor::Render hook.
  void OnCompositorPreRender(void *compositor);
  void OnCompositorRender(
    void *compositor,
    u32 frameIndex,
    bool screenshotPending,
    bool movieMode
  );

  // Called by the SystemIO::IsCapturingMovie detour.
  bool ShouldForceMovie() const;
  bool IsMovieModeActive() const { return m_movieHookInstalled.load(); }

  // Called from HTModRenderGui().
  void OnGui(float deltaTime);

  // Hotkey handlers.
  void ToggleMenu();
  void RequestToggleCapture();
  void OnDisable();

  bool IsMenuOpen() const { return m_menuOpen.load(); }
  bool IsCapturing() const { return m_capturing.load(); }

private:
  struct QueuedFrame {
    std::vector<u08> pixels;
    i32 width = 0;
    i32 height = 0;
    Sky::SourcePixelFormat format = Sky::SourcePixelFormat::Unknown;
    i64 ptsMs = 0;
  };

  void StartCapture();
  void StopCapture(bool join);
  void ToggleCapture();
  void WorkerLoop();
  void ReapWorker();

  bool EnsureMovieHook(void *compositor);
  void RemoveMovieHook();

  std::string ResolveOutputPath(std::string &error);
  void SetStatus(const char *format, ...);
  std::string GetStatus();

  void DrawOverlay();
  void DrawPanel();
  void BrowseOutput();
  void OpenCapturesFolder();

  void SyncSettingsToUi();
  void SyncUiToSettings();

  std::atomic<bool> m_initialized{false};
  HMODULE m_module = nullptr;
  std::wstring m_modFolder;
  std::wstring m_configPath;
  std::wstring m_capturesFolder;

  Settings m_settings;
  mutable std::mutex m_settingsMutex;
  char m_outputBuffer[1024] = {0};

  Sky::FrameGrabber m_grabber;
  std::atomic<bool> m_capturing{false};
  std::atomic<bool> m_forceMovie{true};
  std::atomic<bool> m_showOverlay{true};
  std::atomic<i32> m_targetFps{30};
  std::atomic<i32> m_queueLimit{4};
  std::atomic<bool> m_screenshotPendingThisFrame{false};

  // Capture session.
  std::thread m_worker;
  std::mutex m_queueMutex;
  std::condition_variable m_queueCondition;
  std::deque<QueuedFrame> m_queue;
  std::vector<std::vector<u08>> m_freeBuffers;
  std::atomic<bool> m_stopWorker{false};
  std::atomic<bool> m_workerDone{true};
  std::atomic<bool> m_failed{false};
  std::atomic<u64> m_capturedFrames{0};
  std::atomic<u64> m_encodedFrames{0};
  std::atomic<u64> m_droppedFrames{0};
  std::atomic<i32> m_encoderWidth{0};
  std::atomic<i32> m_encoderHeight{0};

  std::string m_activeOutput;
  std::string m_activeCodec;
  i32 m_activeBitrate = 12000;
  i32 m_activeFps = 30;

  std::chrono::steady_clock::time_point m_startTime;
  std::chrono::steady_clock::time_point m_lastGrabTime;
  u64 m_lastGrabFailLogMs = 0;

  // Movie hook.
  std::atomic<bool> m_movieHookInstalled{false};
  std::mutex m_movieHookMutex;
  u32 m_movieHookAttempts = 0;
  bool m_movieHookWarned = false;

  // UI.
  bool m_imguiReady = false;
  std::atomic<bool> m_menuOpen{false};
  std::atomic<bool> m_toggleCaptureRequested{false};

  mutable std::mutex m_statusMutex;
  std::string m_status = "Idle.";
};

extern LightFlowMod *g_lightFlow;

// Set only while the hooked Compositor::Render() is running. The
// SystemIO::IsCapturingMovie detour consults this flag, so the rest of the
// game (main loop, platform integration, input) still sees the real value.
extern thread_local bool g_forceMovieThisRender;

} // namespace LightFlow

#endif
