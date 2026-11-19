#include "skylink/skyCompositor.hpp"

METABINARY_LINK_MFN_DEF(
  void,
  Compositor,
  Render,
  0,
  (
    Scene *scene,
    Camera *camera,
    Ambience *ambience,
    Ocean *ocean,
    JobBarn *jobBarn,
    CameraSystem *cameraSystem,
    SystemUI *systemUI
  ),
  (
    scene,
    camera,
    ambience,
    ocean,
    jobBarn,
    cameraSystem,
    systemUI
  )
)
