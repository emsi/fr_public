// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
#include <functional>

namespace wz3 {
class ParticleRandom;
using ParticleSpatialPath=std::function<SceneMatrix(float)>;
struct SceneParticle {
  unsigned index{};
  float fraction{};
  SceneMatrix transform=scene_identity();
};
// Scene_Particles is a deterministic mesh-instancing operation, distinct from
// the stateful PartEmitter/PartSystem effects. Cache seeded distributions while
// evaluating motion, splines and rotations at the current animation time.
class SceneParticles {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  explicit SceneParticles(const SplineLibrary&,std::shared_ptr<ParticleRandom> random={},bool released_handler=false);
  ~SceneParticles();
  std::vector<SceneParticle> evaluate(std::size_t operator_id,
      std::span<const std::uint32_t> parameters,std::optional<std::size_t> spline={},const ParticleSpatialPath& spatial_path={});
};
} // namespace wz3
