// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
namespace wz3 {
class ParticleRandom;
struct EffectParticle {
  std::array<float,4> position{},speed{};
  std::int32_t age{},lifetime{};
};
class EffectParticles {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  explicit EffectParticles(std::shared_ptr<ParticleRandom> random={});
  ~EffectParticles();
  void initialize(unsigned slot,unsigned capacity);
  void emit(std::size_t instance,std::span<const std::uint32_t> parameters,
            const SceneMatrix&,const FrameClock&);
  // Call only when the source material's first pass exists. Draw before update;
  // a slot may be advanced more than once by multiple effect jobs in a frame.
  std::vector<SceneEffectVertex> draw(std::span<const std::uint32_t> parameters,
            const SceneCamera&,const SceneMatrix&,const FrameClock&);
  std::span<const EffectParticle> particles(unsigned slot) const;
};
} // namespace wz3
