// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
namespace wz3 {
struct SplinePose {SceneMatrix world=scene_identity();float zoom=1;};
// Cached spatial splines (GenSpline), distinct from animation channel curves.
class SpatialSplines {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  SpatialSplines(const Project&,const Graph&);
  ~SpatialSplines();
  SpatialSplines(const SpatialSplines&)=delete;
  SpatialSplines& operator=(const SpatialSplines&)=delete;
  void precalculate(std::size_t,AnimationEnvironment&);
  SplinePose evaluate(std::size_t,float time,float phase,const AnimationEnvironment&,const EventTimeline* = nullptr);
};
}
