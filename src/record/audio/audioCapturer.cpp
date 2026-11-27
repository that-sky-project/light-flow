#include "record/audio/audioCapturer.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK and friends are hidden unless
// the target version is at least Windows 10 2004. The project does not set
// NTDDI_VERSION itself, so pin it here before the first Windows header.
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A00000A // NTDDI_WIN10_FE
#endif

#include <Windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>

namespace {

constexpr i32 kSampleRate = 44100;
constexpr i32 kChannels = 2;
constexpr i32 kBitsPerSample = 16;
constexpr DWORD kActivationTimeoutMs = 5000;

std::string FormatHResult(cstring what, HRESULT hr) {
  char buffer[192];
  snprintf(buffer, sizeof(buffer), "%s (0x%08X)", what, (unsigned)hr);
  return buffer;
}

template<typename T>
void SafeRelease(T **object) {
  if (*object) {
    (*object)->Release();
    *object = nullptr;
  }
}

// Receives the result of ActivateAudioInterfaceAsync(). The completion is
// delivered on an MTA thread-pool thread, so the handler is agile and owns the
// event the caller waits on; the event therefore outlives the async operation
// even when the caller stops waiting early.
class ActivationHandler final
  : public IActivateAudioInterfaceCompletionHandler
  , public IAgileObject {
public:
  ActivationHandler() {
    m_completedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  }

  ~ActivationHandler() {
    SafeRelease(&m_client);
    if (m_completedEvent)
      CloseHandle(m_completedEvent);
  }

  HANDLE CompletedEvent() const { return m_completedEvent; }
  HRESULT Result() const { return m_result; }

  IAudioClient *TakeClient() {
    IAudioClient *client = m_client;
    m_client = nullptr;
    return client;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(
    REFIID riid,
    void **object
  ) override {
    if (!object)
      return E_POINTER;

    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
      *object = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
      AddRef();
      return S_OK;
    }

    if (riid == __uuidof(IAgileObject)) {
      *object = static_cast<IAgileObject *>(this);
      AddRef();
      return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(++m_references);
  }

  ULONG STDMETHODCALLTYPE Release() override {
    LONG references = --m_references;

    if (references == 0)
      delete this;

    return static_cast<ULONG>(references);
  }

  HRESULT STDMETHODCALLTYPE ActivateCompleted(
    IActivateAudioInterfaceAsyncOperation *operation
  ) override {
    IUnknown *unknown = nullptr;
    HRESULT activateResult = E_FAIL;
    HRESULT hr = operation->GetActivateResult(&activateResult, &unknown);

    if (SUCCEEDED(hr) && SUCCEEDED(activateResult) && unknown) {
      hr = unknown->QueryInterface(
        __uuidof(IAudioClient),
        reinterpret_cast<void **>(&m_client));
    } else if (SUCCEEDED(hr)) {
      hr = activateResult;
    }

    SafeRelease(&unknown);

    m_result = (SUCCEEDED(hr) && m_client)
      ? S_OK
      : (FAILED(hr) ? hr : E_NOINTERFACE);
    if (m_completedEvent)
      SetEvent(m_completedEvent);

    return S_OK;
  }

private:
  std::atomic<LONG> m_references{1};
  HANDLE m_completedEvent = nullptr;
  IAudioClient *m_client = nullptr;
  HRESULT m_result = E_FAIL;
};

} // namespace

// ----------------------------------------------------------------------------
// [SECTION] Impl
// ----------------------------------------------------------------------------

struct AudioCapturer::Impl {
  enum class State {
    kPending = 0,
    kSuccess,
    kFailed
  };

  std::thread thread;
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> done{true};
  std::atomic<bool> ready{false};

  // Created on the first Start() and closed by the destructor. Worker threads
  // may call RequestStop() concurrently with a join, so it must never be
  // closed between sessions.
  HANDLE stopEvent = nullptr;

  std::mutex mutex;
  std::condition_variable condition;
  State state = State::kPending;
  std::string error;

  std::function<void(const i16 *samples, i32 frames, i64 ptsMs)> callback;
  std::function<void()> onExit;

  i64 sessionStartQpc = 0;
  i64 qpcFrequency = 1;
};

// ----------------------------------------------------------------------------
// [SECTION] Lifecycle
// ----------------------------------------------------------------------------

AudioCapturer::AudioCapturer()
  : m_impl(new Impl()) {
}

AudioCapturer::~AudioCapturer() {
  RequestStop();
  Join();

  Impl &impl = *m_impl;
  if (impl.stopEvent) {
    CloseHandle(impl.stopEvent);
    impl.stopEvent = nullptr;
  }
}

bool AudioCapturer::Start(
  std::function<void(const i16 *, i32, i64)> callback,
  i64 sessionStartQpc,
  std::function<void()> onExit,
  std::string &error
) {
  Impl &impl = *m_impl;

  // A previous session must be fully gone before the events are recreated.
  RequestStop();
  Join();

  impl.callback = std::move(callback);
  impl.onExit = std::move(onExit);
  impl.sessionStartQpc = sessionStartQpc;
  impl.stopRequested = false;
  impl.done = false;
  impl.ready = false;
  {
    std::lock_guard<std::mutex> lock(impl.mutex);
    impl.state = Impl::State::kPending;
    impl.error.clear();
  }

  // Publish a synchronous start failure so a later WaitUntilReady() returns
  // immediately instead of waiting for its timeout.
  auto failStart = [&impl, &error](const std::string &message) {
    error = message;
    {
      std::lock_guard<std::mutex> lock(impl.mutex);
      impl.state = Impl::State::kFailed;
      impl.error = message;
    }
    impl.done = true;
    impl.condition.notify_all();
    return false;
  };

  LARGE_INTEGER frequency = {};
  if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    return failStart("QueryPerformanceFrequency failed");
  impl.qpcFrequency = frequency.QuadPart;

  // The stop event lives for the whole AudioCapturer lifetime. Worker threads
  // may call RequestStop() concurrently with a join, so it must never be
  // closed between sessions.
  if (!impl.stopEvent) {
    impl.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!impl.stopEvent)
      return failStart("cannot create the audio capture stop event");
  } else {
    ResetEvent(impl.stopEvent);
  }

  try {
    impl.thread = std::thread(&AudioCapturer::ThreadMain, this);
  } catch (...) {
    return failStart("cannot create the audio capture thread");
  }

  return true;
}

void AudioCapturer::ThreadMain() {
  Impl &impl = *m_impl;
  std::string error;

  bool success = Run(error);

  if (!success) {
    std::lock_guard<std::mutex> lock(impl.mutex);
    if (impl.state == Impl::State::kPending) {
      impl.state = Impl::State::kFailed;
      impl.error = error.empty() ? "audio capture failed" : error;
    }
  }
  impl.condition.notify_all();

  impl.done = true;

  // Runs after the last callback: the sink may now be torn down safely.
  if (impl.onExit)
    impl.onExit();
}

bool AudioCapturer::WaitUntilReady(i32 timeoutMs, std::string &error) {
  Impl &impl = *m_impl;
  std::unique_lock<std::mutex> lock(impl.mutex);

  bool finished = impl.condition.wait_for(
    lock,
    std::chrono::milliseconds(timeoutMs),
    [&impl]() { return impl.state != Impl::State::kPending; });

  if (!finished) {
    error = "audio capture did not initialize in time";
    return false;
  }

  if (impl.state != Impl::State::kSuccess) {
    error = impl.error.empty() ? "audio capture failed" : impl.error;
    return false;
  }

  return true;
}

void AudioCapturer::RequestStop() {
  Impl &impl = *m_impl;
  impl.stopRequested = true;

  // Setting a manual-reset event is idempotent, so this is safe even when the
  // capture thread has not started waiting yet.
  if (impl.stopEvent)
    SetEvent(impl.stopEvent);
}

void AudioCapturer::Join() {
  Impl &impl = *m_impl;

  if (impl.thread.joinable())
    impl.thread.join();
}

bool AudioCapturer::IsRunning() const {
  return m_impl->thread.joinable();
}

bool AudioCapturer::IsDone() const {
  return m_impl->done.load();
}

bool AudioCapturer::IsReady() const {
  return m_impl->ready.load();
}

i32 AudioCapturer::SampleRate() const {
  return kSampleRate;
}

i32 AudioCapturer::Channels() const {
  return kChannels;
}

// ----------------------------------------------------------------------------
// [SECTION] Capture
// ----------------------------------------------------------------------------

bool AudioCapturer::Run(std::string &error) {
  Impl &impl = *m_impl;

  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    error = FormatHResult("CoInitializeEx failed", hr);
    return false;
  }

  HANDLE sampleEvent = nullptr;
  IAudioClient *audioClient = nullptr;
  IAudioCaptureClient *captureClient = nullptr;
  bool audioStarted = false;

  bool initialized = false;
  do {
    sampleEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!sampleEvent) {
      error = "cannot create the sample-ready event";
      break;
    }

    // 1. Activate the process-loopback virtual audio device for this process
    //    tree. The activation is asynchronous and completes on a thread-pool
    //    thread; the handler signals us when the IAudioClient is ready.
    AUDIOCLIENT_ACTIVATION_PARAMS parameters = {};
    parameters.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    parameters.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    parameters.ProcessLoopbackParams.ProcessLoopbackMode =
      PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activateParameters = {};
    activateParameters.vt = VT_BLOB;
    activateParameters.blob.cbSize = sizeof(parameters);
    activateParameters.blob.pBlobData =
      reinterpret_cast<BYTE *>(&parameters);

    auto *handler = new ActivationHandler();
    if (!handler->CompletedEvent()) {
      error = "cannot create the activation event";
      handler->Release();
      break;
    }

    IActivateAudioInterfaceAsyncOperation *operation = nullptr;
    hr = ActivateAudioInterfaceAsync(
      VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
      __uuidof(IAudioClient),
      &activateParameters,
      handler,
      &operation);

    if (FAILED(hr)) {
      error = FormatHResult("ActivateAudioInterfaceAsync failed", hr);
      handler->Release();
      break;
    }

    HANDLE waitHandles[2] = {handler->CompletedEvent(), impl.stopEvent};
    DWORD wait = WaitForMultipleObjects(
      2, waitHandles, FALSE, kActivationTimeoutMs);

    if (wait == WAIT_OBJECT_0) {
      audioClient = handler->TakeClient();
      if (!audioClient)
        error = FormatHResult("audio activation failed", handler->Result());
    } else if (wait == WAIT_OBJECT_0 + 1) {
      error = "audio capture start was cancelled";
    } else if (wait == WAIT_TIMEOUT) {
      error = "audio activation timed out";
    } else {
      error = FormatHResult(
        "waiting for audio activation failed",
        HRESULT_FROM_WIN32(GetLastError()));
    }

    SafeRelease(&operation);

    if (wait == WAIT_OBJECT_0) {
      handler->Release();
    } else {
      // The completion callback has not run yet. Windows may still call it,
      // and it must not be destroyed before that happens. Deliberately keep
      // our reference instead of risking a use-after-free; this only happens
      // when activation is abandoned (stop request or timeout), and the mod
      // is never unloaded.
      handler = nullptr;
    }

    if (!audioClient)
      break;

    // 2. The process-loopback virtual device always converts to the caller's
    //    format; AUTOCONVERTPCM lets us ask for a stable PCM layout instead of
    //    chasing the endpoint's mix format.
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(kChannels);
    format.nSamplesPerSec = static_cast<DWORD>(kSampleRate);
    format.wBitsPerSample = static_cast<WORD>(kBitsPerSample);
    format.nBlockAlign = static_cast<WORD>(
      format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    hr = audioClient->Initialize(
      AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_LOOPBACK |
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
      0,
      0,
      &format,
      nullptr);
    if (FAILED(hr)) {
      error = FormatHResult("IAudioClient::Initialize failed", hr);
      break;
    }

    hr = audioClient->SetEventHandle(sampleEvent);
    if (FAILED(hr)) {
      error = FormatHResult("IAudioClient::SetEventHandle failed", hr);
      break;
    }

    hr = audioClient->GetService(
      IID_PPV_ARGS(&captureClient));
    if (FAILED(hr)) {
      error = FormatHResult("IAudioClient::GetService failed", hr);
      break;
    }

    hr = audioClient->Start();
    if (FAILED(hr)) {
      error = FormatHResult("IAudioClient::Start failed", hr);
      break;
    }
    audioStarted = true;

    initialized = true;
  } while (false);

  if (!initialized) {
    if (captureClient)
      SafeRelease(&captureClient);
    if (audioStarted && audioClient)
      audioClient->Stop();
    if (audioClient)
      SafeRelease(&audioClient);
    if (sampleEvent)
      CloseHandle(sampleEvent);
    CoUninitialize();
    return false;
  }

  // Publish the successful initialization so the encoder worker can add the
  // audio stream. Audio buffers may already be delivered by now; the caller
  // buffers them until the encoder opens.
  {
    std::lock_guard<std::mutex> lock(impl.mutex);
    impl.state = Impl::State::kSuccess;
  }
  impl.ready = true;
  impl.condition.notify_all();

  // GetBuffer() reports pu64QPCPosition in 100-nanosecond units (the raw
  // counter scaled by 10^7 / frequency), while sessionStartQpc is a raw
  // QueryPerformanceCounter() value. Convert the session start once; the
  // split form keeps the multiplication from overflowing on long uptimes.
  const i64 kHundredNsPerSecond = 10000000;
  i64 sessionStart100ns =
    (impl.sessionStartQpc / impl.qpcFrequency) * kHundredNsPerSecond +
    (impl.sessionStartQpc % impl.qpcFrequency) * kHundredNsPerSecond /
      impl.qpcFrequency;

  // Fallback timeline for drivers that do not report QPC positions.
  LARGE_INTEGER now = {};
  QueryPerformanceCounter(&now);
  i64 fallbackPtsMs = (now.QuadPart - impl.sessionStartQpc) * 1000 /
                      impl.qpcFrequency;
  if (fallbackPtsMs < 0)
    fallbackPtsMs = 0;

  std::vector<i16> silence;

  while (!impl.stopRequested.load()) {
    HANDLE waitHandles[2] = {impl.stopEvent, sampleEvent};
    DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

    if (wait != WAIT_OBJECT_0 + 1)
      break; // Stop requested (or the wait failed).

    UINT32 packetFrames = 0;
    while (SUCCEEDED(captureClient->GetNextPacketSize(&packetFrames)) &&
           packetFrames > 0) {
      BYTE *data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      UINT64 qpcPosition = 0;

      hr = captureClient->GetBuffer(
        &data, &frames, &flags, nullptr, &qpcPosition);
      if (FAILED(hr))
        break;

      if (frames == 0) {
        captureClient->ReleaseBuffer(0);
        continue;
      }

      i64 ptsMs;
      if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) &&
          qpcPosition != 0) {
        i64 delta = static_cast<i64>(qpcPosition) - sessionStart100ns;
        if (delta < 0)
          delta = 0;
        ptsMs = delta / 10000; // 100 ns -> ms
      } else {
        ptsMs = fallbackPtsMs;
      }

      fallbackPtsMs = ptsMs +
        static_cast<i64>(frames) * 1000 / kSampleRate;

      const i16 *samples = reinterpret_cast<const i16 *>(data);
      if (!samples || (flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
        usize required = static_cast<usize>(frames) * kChannels;
        if (silence.size() < required)
          silence.resize(required, 0);
        samples = silence.data();
      }

      if (impl.callback)
        impl.callback(samples, static_cast<i32>(frames), ptsMs);

      captureClient->ReleaseBuffer(frames);
    }
  }

  audioClient->Stop();
  SafeRelease(&captureClient);
  SafeRelease(&audioClient);
  CloseHandle(sampleEvent);
  CoUninitialize();
  return true;
}
