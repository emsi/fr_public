// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"

namespace wz3 {
// Texture slots retain Material20's numbering (0..3 color/mask, 4..5 bump,
// 6 environment). Slot 7 is the captured prelight image, not a graph bitmap.
struct Material20Source {
  std::string vertex,fragment;
  std::vector<unsigned> samplers;
};
Material20Source material20_source(const Material20Shader&);
std::uint32_t material20_sampler_flags(const Material20Shader&,unsigned slot);
std::array<float,9> material20_uv_matrix(const Material20Shader&,unsigned slot);
std::array<float,12> material20_color_matrix(const Material20Shader&);
struct Material20Lighting {
  Vec3 position,eye;
  std::array<float,4> attenuation,diffuse,specular;
  bool directional{};
};
Material20Lighting material20_lighting(const Material20Shader&,const SceneMatrix&,const SceneCamera&,const SceneLight&);
SceneMatrix material20_environment_matrix(const Material20Shader&,const SceneMatrix&,const SceneCamera&);
} // namespace wz3
