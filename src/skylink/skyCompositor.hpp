#ifndef __SKYLINK_SKYCOMPOSITOR_HPP__
#define __SKYLINK_SKYCOMPOSITOR_HPP__

#include "utils/types.hpp"
#include "metabinary/metabinary.hpp"
#include "skylink/skyTypePlaceholders.hpp"

class Compositor {
public:
  METABINARY_LINK_MFN(void, Compositor, Render, 0,
    (JobBarn *jobBarn, CameraSystem *cameraSystem, SystemUI *systemUI),
    (jobBarn, cameraSystem, systemUI)
  )
};

// Compositor field layouts. The two releases are built from different engine
// revisions, so nearly every member moved. Keep the offsets next to the
// structure they describe; the active layout is selected in record/lightflow.
struct CompositorLayout {
  usize outputMode;
  usize screenshotRequested;
  usize frameCounter;
  usize systemIO;
  usize renderTargetBarn;
  usize renderTargetArray;
};

// Sky_dump (NetEase / Chinese edition).
inline constexpr CompositorLayout kCompositorLayoutNetease = {
  /* outputMode */ 4132,
  /* screenshotRequested */ 4230,
  /* frameCounter */ 6080,
  /* systemIO */ 6248,
  /* renderTargetBarn */ 6320,
  /* renderTargetArray */ 337016,
};

// Sky-32.2-260129 (TGC / international edition).
inline constexpr CompositorLayout kCompositorLayoutTgc = {
  /* outputMode */ 12,
  /* screenshotRequested */ 88,
  /* frameCounter */ 316,
  /* systemIO */ 480,
  /* renderTargetBarn */ 544,
  /* renderTargetArray */ 146696,
};

// VulkanRenderer's texture bookkeeping, as read by FrameGrabber.
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
