#ifndef __SKYLINK_SKYSYSTEMIO_HPP__
#define __SKYLINK_SKYSYSTEMIO_HPP__

#include "metabinary/metabinary.hpp"

class SystemIO {
public:
  virtual ~SystemIO() = default;

  METABINARY_LINK_MVFN(bool, SystemIO, IsCapturingMovie, 0, (), ())
  // The NetEase version uses CCLive for in-game recording, which is not the same
  // detection function as the video capture that already exists in both versions.
  METABINARY_LINK_MVFN(bool, SystemIO, IsCapturingMovieCC, 0, (), ())
};

#endif
