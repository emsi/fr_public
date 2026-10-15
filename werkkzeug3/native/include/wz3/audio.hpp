// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/playback_timing.hpp"

namespace wz3 {
struct PcmAudio {
  unsigned rate{},channels{};
  std::vector<float> samples; // interleaved, native float
  std::uint64_t frames() const {return channels ? samples.size()/channels : 0;}
};
// Decode before playback; no allocation or decoder work in the audio callback.
PcmAudio read_vorbis(const std::filesystem::path&);
// Synthesize exactly the requested playback interval, including release tails.
PcmAudio read_v2m_audio(const std::filesystem::path&,std::uint64_t frames);

// The callback is the clock authority. Interpolate within its latest buffer,
// stopping at the end if the device stalls. This estimates delivery time, not
// the DAC position: SDL2 does not expose the downstream hardware latency.
// The caller must serialize fill() and position() (SDL's device lock suffices).
class AudioPlayback {
  const PcmAudio& pcm;
  std::uint64_t submitted{},anchor_frame{},anchor_ns{},buffer_frames{},callbacks{};
  bool muted;
public:
  explicit AudioPlayback(const PcmAudio&,bool mute=false);
  void fill(std::span<float> output,std::uint64_t now_ns) noexcept;
  std::uint64_t position(std::uint64_t now_ns) const noexcept;
  std::uint64_t callback_count() const noexcept {return callbacks;}
};

} // namespace wz3
