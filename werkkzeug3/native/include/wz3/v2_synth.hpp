// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/v2m.hpp"
#include <memory>
namespace wz3 {
// C++ V2 DSP from the archive with a bounded module adapter. Historical patch
// versions and Ronan speech require separate support and fail explicitly.
class V2Synthesis {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  explicit V2Synthesis(const V2Module&,unsigned rate=44100,std::uint32_t seed=1);
  ~V2Synthesis();
  V2Synthesis(const V2Synthesis&)=delete;
  V2Synthesis& operator=(const V2Synthesis&)=delete;
  // Stereo float output. Keeps envelopes/delays alive after the last MIDI event.
  void render(std::span<float>);
  std::uint64_t position() const;
};
}
