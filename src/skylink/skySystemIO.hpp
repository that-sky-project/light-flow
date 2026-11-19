#ifndef __SKYLINK_SKYSYSTEMIO_HPP__
#define __SKYLINK_SKYSYSTEMIO_HPP__

#include "metabinary/metabinary.hpp"

class SystemIO {
public:
  virtual ~SystemIO() = default;

  METABINARY_LINK_MVFN(bool, SystemIO, IsCapturingMovie, 0, (), ())
};

#endif
