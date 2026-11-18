#ifndef __SKYLINK_SKYRENDERTARGET_HPP__
#define __SKYLINK_SKYRENDERTARGET_HPP__

class Compositor {
private:
  Compositor() = delete;
  Compositor(const Compositor &) = delete;
  Compositor(Compositor &&) = delete;
  Compositor &operator=(const Compositor &) = delete;

public:
  void Render();
};

#endif