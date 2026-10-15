// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
namespace wz3 {
namespace legacy {struct BlobSpline;struct BlobPipe;}
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
  // Retain a prepared plain/pipe/scaled curve independently of this library.
  // Event-driven shakers require a paint environment and cannot be frozen.
  std::shared_ptr<legacy::BlobSpline> static_curve(std::size_t,AnimationEnvironment&);
  // Pipe meshes also need the original corner positions and bend radii.
  std::pair<std::shared_ptr<legacy::BlobSpline>,std::shared_ptr<legacy::BlobPipe>> pipe_curve(std::size_t,AnimationEnvironment&);
  SplinePose evaluate(std::size_t,float time,float phase,const AnimationEnvironment&,const EventTimeline* = nullptr);
};
}
