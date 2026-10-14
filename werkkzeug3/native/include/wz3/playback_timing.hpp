// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/project.hpp"

namespace wz3 {
// mainplayer.cpp's two sample-to-beat formulas, including Buzz's integer tick
// length. SoundOffset is editor-only and is not exported to the original player.
class PlaybackTiming {
  std::int32_t bpm_fixed{},end_beat{};
  unsigned rate{},buzz_tick{};
  bool strict_endpoint{};
public:
  // Clock conversion also supports projects without a playback endpoint.
  // end_frames() separately requires a valid saved endpoint.
  PlaybackTiming(const Project&,unsigned sample_rate);
  std::int32_t beat(std::uint64_t frames) const;
  std::int32_t milliseconds(std::uint64_t frames) const;
  std::uint64_t samples_at_beat(std::int32_t beat) const; // first sample reaching the requested beat
  std::uint64_t end_frames() const;
};
} // namespace wz3
