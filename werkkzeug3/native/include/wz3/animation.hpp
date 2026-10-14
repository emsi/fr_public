// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/project.hpp"
#include <map>
#include <optional>

namespace wz3 {
using AnimationVector = std::array<float,4>;

class SplineLibrary {
  struct Curve {unsigned interpolation;std::vector<std::vector<std::array<float,2>>> channels;};
  std::vector<Curve> curves;
  std::map<std::string,std::size_t,std::less<>> names;
public:
  explicit SplineLibrary(const Project&);
  std::optional<std::size_t> find(std::string_view name) const;
  AnimationVector evaluate(std::size_t id,float time) const;
  std::size_t size() const {return curves.size();}
};

struct AnimationProgram {
  // The original KA_* bytecode, emitted explicitly in little-endian order.
  std::vector<std::uint8_t> code;
  std::size_t source_nodes{},used_nodes{};
  std::uint32_t read_variables{},written_variables{};
};
AnimationProgram compile_animation(const Operator&,const SplineLibrary&);

struct AnimationEnvironment {
  std::array<AnimationVector,32> variables{};
  std::optional<std::size_t> event_spline;
};
AnimationEnvironment frame_environment(std::int32_t beat_16_16);

// Keep this scope alive while executing an operator's children. Stores affect
// their environment; destruction restores only the variables this program saved.
// LOADPARA reads original values, even after this program has written parameters.
class AnimationScope {
  AnimationEnvironment& environment;
  std::vector<std::pair<unsigned,AnimationVector>> saved;
  void restore() noexcept;
public:
  std::vector<std::uint32_t> parameters;
  bool changed{}; // KA_CHANGEPARA*, for later mesh/material cache invalidation
  AnimationScope(const AnimationProgram&,const SplineLibrary&,
                 std::span<const std::uint32_t> original,AnimationEnvironment&,
                 std::span<const std::uint32_t> previous = {});
  ~AnimationScope();
  AnimationScope(const AnimationScope&)=delete;
  AnimationScope& operator=(const AnimationScope&)=delete;
};

bool event_active(const Event&,std::int32_t beat_16_16);
class EventAnimationScope {
  AnimationEnvironment& environment;
  std::array<AnimationVector,8> saved;
  std::optional<std::size_t> saved_spline;
public:
  float time{};
  EventAnimationScope(const Event&,std::int32_t beat_16_16,const SplineLibrary&,AnimationEnvironment&);
  ~EventAnimationScope();
  EventAnimationScope(const EventAnimationScope&)=delete;
  EventAnimationScope& operator=(const EventAnimationScope&)=delete;
};
} // namespace wz3
