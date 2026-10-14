// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
namespace wz3 {
struct SceneFog {std::array<float,4> plane;Vec3 color;};
// sMaterial11::Set's c20, evaluated in model space by material11.vsh.
SceneFog scene_fog(const SceneCamera&,const SceneMatrix& model);
// Append the fixed-function RGB fog stage after the saved pixel shader.
std::string fog_fragment_shader(std::string source);
} // namespace wz3
