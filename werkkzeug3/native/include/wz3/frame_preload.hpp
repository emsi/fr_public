// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/frame.hpp"
namespace wz3 {
// Inspect EVERY simulation frame, but request GPU draws only for newly visible
// resource configurations. This is not a playback frame-skipping policy.
class FramePreloader {
  struct Impl;std::unique_ptr<Impl> impl;
public:
  FramePreloader();
  ~FramePreloader();
  bool needs_draw(const DemoFrame&);
};
}
