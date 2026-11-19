#ifndef __RECORD_LIGHTFLOW_HPP__
#define __RECORD_LIGHTFLOW_HPP__

// ----------------------------------------------------------------------------
// [SECTION] LightFlowUi
// ----------------------------------------------------------------------------

class LightFlowUi {
public:
  ~LightFlowUi() = default;
  LightFlowUi() = default;
  LightFlowUi(const LightFlowUi &) = delete;
  LightFlowUi(LightFlowUi &&) = delete;
  LightFlowUi &operator=(const LightFlowUi &) = delete;

  void Initialize();
  void BuildScene();

private:
};

// ----------------------------------------------------------------------------
// [SECTION] LightFlowMod
// ----------------------------------------------------------------------------

class LightFlowMod {
public:
  ~LightFlowMod() = default;
  LightFlowMod() = default;
  LightFlowMod(const LightFlowMod &) = delete;
  LightFlowMod(LightFlowMod &&) = delete;
  LightFlowMod &operator=(const LightFlowMod &) = delete;

  inline bool IsCapturing() const { return m_isCapturing; }

  void Initialize();

  void Update();
  void BuildScene();

private:
  bool m_isCapturing = false;
  LightFlowUi *m_lightFlowUi = nullptr;
};

LightFlowMod *GetLightFlowMod();

#endif
