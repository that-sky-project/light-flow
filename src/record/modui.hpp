#ifndef __RECORD_MODUI_HPP__
#define __RECORD_MODUI_HPP__

#include "record/lightflow.hpp"

class LightFlowMod;

// ----------------------------------------------------------------------------
// [SECTION] LightFlowUi
// ----------------------------------------------------------------------------

class LightFlowUi {
public:
  LightFlowUi() = default;
  ~LightFlowUi() = default;
  LightFlowUi(const LightFlowUi &) = delete;
  LightFlowUi(LightFlowUi &&) = delete;
  LightFlowUi &operator=(const LightFlowUi &) = delete;

  void Initialize();
  void BuildScene();

  inline void ToggleMenu() { m_menuOpen = !m_menuOpen; }
  inline bool IsMenuOpen() const { return m_menuOpen; }

private:
  void m_DrawOverlay(LightFlowMod *mod);
  void m_DrawPanel(LightFlowMod *mod);
  void m_BrowseOutput(LightFlowMod *mod);
  void m_OpenCapturesFolder(LightFlowMod *mod);

  char m_outputBuffer[1024] = {0};
  bool m_menuOpen = false;
};

#endif
