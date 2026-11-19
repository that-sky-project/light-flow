// Reads the rtScreenshot render target out of a live Compositor.
class FrameGrabber {
public:
  void SetCompositor(void *compositor) { m_compositor = compositor; }
  void *Compositor() const { return m_compositor; }

  // Resolve the SystemIO object owned by the compositor.
  static void *GetSystemIO(void *compositor);

  // Read the current rtScreenshot pixels into `out`. `frameIndex` must be the
  // compositor's frame counter as it was before the render call (the render
  // graph uses it to select the ping-pong texture).
  bool Grab(
    u32 frameIndex,
    std::vector<u08> &out,
    i32 &width,
    i32 &height,
    SourcePixelFormat &pixelFormat,
    u32 &gfxFormat,
    u32 &vkFormat
  );

  const std::string &LastError() const { return m_lastError; }

private:
  bool ResolveTarget(RenderTarget **outTarget);
  void Fail(cstring fmt, ...);

  void *m_compositor = nullptr;
  std::string m_lastError;
};