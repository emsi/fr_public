// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/timeline.hpp"
#include "wz3/playback_timing.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <stdexcept>

namespace wz3 {
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct Nearest {int previous=std::fegetround();Nearest(){std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetround(previous);}};
}
void FrameClock::advance(std::int32_t milliseconds) {
  require(milliseconds>=0,"negative frame clock");reset=milliseconds<current;delta=reset ? 0 : milliseconds-current;current=milliseconds;
}
std::int32_t beat_milliseconds(std::int32_t beat,float bpm) {
  const Nearest rounding;require(beat>=0 && std::isfinite(bpm) && bpm>0,"beat clock requires nonnegative beat and positive BPM");
  const auto ms=double(beat)*60000/(65536.0*bpm);require(ms<=2147483647,"beat clock overflow");return std::int32_t(ms);
}
std::int32_t beat_milliseconds(std::int32_t beat,const Project& project) {
  const PlaybackTiming timing(project,44100);
  return timing.milliseconds(timing.samples_at_beat(beat));
}
EventTimeline::EventTimeline(const Project& p,const Graph& graph):project(p),events(p.events),targets(events.size()),active_events(p.operators.size()) {
  require(graph.nodes.size()==p.operators.size(),"timeline graph size mismatch");
  for(std::size_t i=0;i<events.size();++i) {
    const auto found=graph.names.find(events[i].name);
    if(found!=graph.names.end()) {
      require(found->second<active_events.size() && graph.nodes[found->second].active,"invalid timeline event target");
      targets[i]=found->second;
    }
  }
}
void EventTimeline::reset() {
  events=project.events;prepared=false;
  for(auto& list:active_events)list.clear();
}
void EventTimeline::begin_frame(std::int32_t beat) {
  frame_beat=beat;prepared=true;
  for(auto& list:active_events)list.clear();
  // The archive prepends each event to FirstEvent. Reverse file order is
  // significant when events overlap, even if their start times are identical.
  for(std::size_t i=events.size();i>0;) {
    --i;if(targets[i] && event_active(events[i],beat))active_events[*targets[i]].push_back(i);
  }
}
std::int32_t EventTimeline::beat() const {require(prepared,"timeline frame is not prepared");return frame_beat;}
std::span<const std::size_t> EventTimeline::active(std::size_t id) const {
  require(prepared,"timeline frame is not prepared");require(id<active_events.size(),"invalid timeline operator");return active_events[id];
}
const Event& EventTimeline::event(std::size_t id) const {
  require(id<events.size(),"invalid timeline event");return events[id];
}
const Event& EventTimeline::start(std::size_t id,float duration) {
  require(prepared,"timeline frame is not prepared");require(id<events.size(),"invalid timeline event");
  auto& value=events[id];
  if(value.fields[1]==value.fields[2]) {
    require(std::isfinite(duration) && duration>=0,"invalid default event duration");
    const int saved=std::fegetround();require(saved!=-1 && std::fesetround(FE_TONEAREST)==0,"cannot set event rounding mode");
    const double ticks=std::nearbyint(double(duration)*65536);
    std::fesetround(saved);
    require(ticks<=2147483647.0,"event duration overflow");
    const auto end=std::int64_t(frame_beat)+std::int64_t(ticks);
    require(end>=-2147483648LL && end<=2147483647LL,"event end time overflow");
    value.fields[1]=std::bit_cast<std::uint32_t>(frame_beat);value.fields[2]=std::bit_cast<std::uint32_t>(std::int32_t(end));
  }
  return value;
}
} // namespace wz3
