#ifndef __SKYLINK_SKYCOMPOSITOR_HPP__
#define __SKYLINK_SKYCOMPOSITOR_HPP__

#include "utils/types.hpp"
#include "metabinary/metabinary.hpp"
#include "skylink/skySystemIO.hpp"

class JobBarn;
class CameraSystem;
class SystemUI;

class Compositor {
public:
  METABINARY_LINK_MFN(void, Compositor, Render, 0,
    (JobBarn *jobBarn, CameraSystem *cameraSystem, SystemUI *systemUI),
    (jobBarn, cameraSystem, systemUI)
  )

  METABINARY_LINK_MVAR(SystemIO *, Compositor, systemIO)
  METABINARY_LINK_MVAR(bool, Compositor, screenshotRequested)
};

// TODO: We need to remove all layout hardcoding and use hook or metabinary.

struct CompositorLayout {
  usize outputMode;
  usize frameCounter;
};

inline constexpr CompositorLayout kCompositorLayoutNetease = {
  /* outputMode */ 4132,
  /* frameCounter */ 6080
};

inline constexpr CompositorLayout kCompositorLayoutTgc = {
  /* outputMode */ 12,
  /* frameCounter */ 316
};

struct RendererLayout {
  usize textureRecords;
  usize textureRecordStride;
  usize textureRecordWidth;
  usize textureRecordHeight;
  usize textureRecordVkFormat;
  usize textureRecordFormat;
};

inline constexpr RendererLayout kRendererLayoutNetease = {
  /* textureRecords */ 4136,
  /* textureRecordStride */ 184,
  /* textureRecordWidth */ 100,
  /* textureRecordHeight */ 104,
  /* textureRecordVkFormat */ 96,
  /* textureRecordFormat */ 176,
};

inline constexpr RendererLayout kRendererLayoutTgc = {
  /* textureRecords */ 4120,
  /* textureRecordStride */ 184,
  /* textureRecordWidth */ 100,
  /* textureRecordHeight */ 104,
  /* textureRecordVkFormat */ 96,
  /* textureRecordFormat */ 176,
};

#endif
