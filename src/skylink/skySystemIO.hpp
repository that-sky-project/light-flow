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
  // In the NetEase version, when outputMode is video recording, this function is
  // called to determine whether the user is logged in and to transfer the current
  // frame into a texture. Our screen recording path does not allocate that texture,
  // so failing to intercept this call will cause the game to crash.
  METABINARY_LINK_MVFN(bool, SystemIO, IsLoggedIn, 0, (), ())
};

#endif
