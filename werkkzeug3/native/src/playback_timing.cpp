// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/playback_timing.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
}
PlaybackTiming::PlaybackTiming(const Project& project,unsigned sample_rate):rate(sample_rate) {
  const double bpm=as_float(project.settings[4]);
  require(std::isfinite(bpm) && bpm>=1 && bpm<=1000,"invalid playback BPM");
  require(rate>=8000 && rate<=192000,"invalid playback sample rate");
  bpm_fixed=std::int32_t(bpm*65536);end_beat=std::bit_cast<std::int32_t>(project.settings[6]);
  strict_endpoint=project.release_timing;
  if(strict_endpoint)require(bpm_fixed==138*65536 && end_beat==592*65536 && project.settings[9]==1,"invalid released clock profile");
  if(project.settings[9])buzz_tick=60*44100/((bpm_fixed>>16)*8);
}
std::int32_t PlaybackTiming::beat(std::uint64_t frames) const {
  // Also keeps all integer products below 2^64 for unusual caller input.
  require(frames<=std::uint64_t(rate)*24*60*60,"playback time exceeds 24 hours");
  const auto value=buzz_tick ? (frames*44100/rate)*65536/buzz_tick/8 : frames*std::uint64_t(bpm_fixed)/(std::uint64_t(rate)*60);
  require(value<=std::uint64_t(std::numeric_limits<std::int32_t>::max()),"playback beat exceeds 16.16 range");return std::int32_t(value);
}
std::int32_t PlaybackTiming::milliseconds(std::uint64_t frames) const {
  require(frames<=std::uint64_t(rate)*24*60*60,"playback time exceeds 24 hours");return std::int32_t(frames*1000/rate);
}
std::uint64_t PlaybackTiming::samples_at_beat(std::int32_t beat) const {
  require(beat>=0,"sample clock requires a nonnegative beat");
  const auto threshold=std::uint64_t(beat);
  if(buzz_tick) {
    const auto samples44100=(threshold*buzz_tick*8+65535)/65536;
    return (samples44100*rate+44099)/44100;
  }
  return (threshold*rate*60+bpm_fixed-1)/bpm_fixed;
}
std::uint64_t PlaybackTiming::end_frames() const {
  require(end_beat>0,"invalid saved demo endpoint");
  // The recovered release exits on beat > 592. Editor/provisional projects
  // retain their existing first-sample-at-or-after endpoint convention.
  return samples_at_beat(end_beat+(strict_endpoint ? 1 : 0));
}
} // namespace wz3
