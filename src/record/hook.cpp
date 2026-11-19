#include "metabinary/metabinary.hpp"
#include "skylink/skySystemIO.hpp"
#include "record/lightflow.hpp"

class HOOK_SystemIO: public SystemIO {
public:
  bool IsCapturingMovie() {
    return GetLightFlowMod()->IsCapturing();
  }
};

METABINARY_HOOK_MFN(SystemIO, IsCapturingMovie, 0, &HOOK_SystemIO::IsCapturingMovie)
