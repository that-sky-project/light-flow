#ifndef __RECORD_AUDIOCAPTURER_HPP__
#define __RECORD_AUDIOCAPTURER_HPP__

#include <functional>
#include <memory>
#include <string>

#include "utils/types.hpp"

// ----------------------------------------------------------------------------
// [SECTION] AudioCapturer
// ----------------------------------------------------------------------------

// Captures the audio rendered by this process tree with the Windows Audio
// Session API (WASAPI) process-loopback virtual device. Unlike endpoint
// loopback, this records only the game (and its child processes), not the
// audio of every other application.
//
// Process loopback is available on Windows 10 2004 (build 19041) and later.
// Activation and initialization happen on the capture thread, so the game
// thread never blocks on the audio engine. The fixed engine-side format is
// 16-bit PCM, 44.1 kHz, stereo; the audio engine converts from the actual
// render mix on the fly (AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM).
class AudioCapturer {
public:
  AudioCapturer();
  ~AudioCapturer();

  AudioCapturer(const AudioCapturer &) = delete;
  AudioCapturer(AudioCapturer &&) = delete;
  AudioCapturer &operator=(const AudioCapturer &) = delete;

  // Spawns the capture thread. `sessionStartQpc` is a QueryPerformanceCounter()
  // reading taken when the recording started; the callback receives
  // millisecond timestamps relative to it. `onExit` runs on the capture thread
  // after it has stopped touching the sink.
  bool Start(
    std::function<void(const i16 *samples, i32 frames, i64 ptsMs)> callback,
    i64 sessionStartQpc,
    std::function<void()> onExit,
    std::string &error
  );

  // Waits for the asynchronous activation/initialization to finish. Returns
  // true once audio buffers are being delivered. Called from the encoder
  // worker, never from the game thread.
  bool WaitUntilReady(i32 timeoutMs, std::string &error);

  // Safe to call from any thread and before Start(). Wakes the capture thread
  // and makes its waits return.
  void RequestStop();

  // Joins the capture thread. Must not be called from the capture thread.
  void Join();

  bool IsRunning() const;
  bool IsDone() const;
  bool IsReady() const;

  i32 SampleRate() const;
  i32 Channels() const;

private:
  struct Impl;

  void ThreadMain();
  bool Run(std::string &error);

  std::unique_ptr<Impl> m_impl;
};

#endif
