// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
namespace wz3 {
struct WalkPose {
  SceneMatrix body=scene_identity();
  std::array<AnimationVector,8> feet{};
  unsigned count{};
  AnimationVector timing{};
};
// Event/instance-owned gait state, following Exec_Scene_Walk. A pose is sampled
// before advancing the step scheduler; child animation consumes its variables.
class SceneWalk {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  explicit SceneWalk(const SplineLibrary&);
  ~SceneWalk();
  WalkPose evaluate(std::size_t instance,std::span<const std::uint32_t> parameters,
                    const SceneMatrix&,const FrameClock&,std::optional<std::size_t> spline={});
};
struct LimbPose {
  std::array<SceneMatrix,2> world;
  // Each child receives the OTHER limb's local matrix in KV_MATRIX_I..L.
  std::array<SceneMatrix,2> other;
};
LimbPose scene_limb(std::span<const std::uint32_t>,const SceneMatrix&);
SceneMatrix scene_rotate(std::span<const std::uint32_t>,const SceneMatrix&,const SceneCamera&);
} // namespace wz3
