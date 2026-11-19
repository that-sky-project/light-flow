#include "utils/assert.hpp"
#include "record/lightflow.hpp"

static LightFlowMod *g_LightFlowMod = nullptr;

// ----------------------------------------------------------------------------
// [SECTION] LightFlowUi
// ----------------------------------------------------------------------------

void LightFlowUi::Initialize() {

}

void LightFlowUi::BuildScene() {
  
}

// ----------------------------------------------------------------------------
// [SECTION] LightFlowMod
// ----------------------------------------------------------------------------

void LightFlowMod::Initialize() {
  if (g_LightFlowMod)
    return;

  g_LightFlowMod = this;

  m_lightFlowUi = new LightFlowUi();
}

LightFlowMod *GetLightFlowMod() {
  ASSERTMSG(g_LightFlowMod, "LightFlowMod is uninitialized.");
  return g_LightFlowMod;
}
