// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/animation.hpp"
#include "wz3/graph.hpp"

namespace wz3 {
// Millisecond state shared by gait and particle simulation. Backward time
// signals a reset and suppresses this frame's elapsed update.
struct FrameClock {
  std::int32_t current{},delta{};
  bool reset{};
  void advance(std::int32_t milliseconds);
};
// Convenience for offline sampling. Actual playback can supply audio-derived
// beat and wall-clock milliseconds independently, as the original player did.
std::int32_t beat_milliseconds(std::int32_t beat,float bpm);

// KEnvironment::AddStaticEvent and Exec_Misc_Event. A timeline owns its mutable
// event times: starting a zero-length event must never modify the saved project.
// Reuse it across frames; reset() restarts those events from their saved values.
class EventTimeline {
  const Project& project;
  std::vector<Event> events;
  std::vector<std::optional<std::size_t>> targets;
  std::vector<std::vector<std::size_t>> active_events;
  std::int32_t frame_beat{};
  bool prepared{};
public:
  EventTimeline(const Project&,const Graph&);
  void reset();
  void begin_frame(std::int32_t beat_16_16);
  std::int32_t beat() const;
  std::span<const std::size_t> active(std::size_t operator_id) const;
  const Event& event(std::size_t event_id) const;
  const Event& start(std::size_t event_id,float default_duration);
};
} // namespace wz3
