// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
namespace wz3 {
// _start.cpp MakeCubeNormalizer and MakeAttenuationVolume, in RGBA order.
struct LightingTables {std::vector<std::uint8_t> cube,attenuation;};
LightingTables lighting_tables();
struct SignedNormalLevel {unsigned width,height;std::vector<std::int8_t> pixels;};
// Released x87 conversion rounds to nearest-even; the later editor truncates.
std::vector<SignedNormalLevel> signed_normal_levels(const Texture&,bool released=false);
// sMaterial11::Set uses TransR (transpose rotation), even for scaled models.
Vec3 light_model_vector(const SceneMatrix&,const Vec3&,bool point);
std::array<float,4> light_color(const MaterialShader&,const SceneLight&);
std::array<float,2> light_specular_parameters(float power);
std::string material_light_fragment_shader(const MaterialShader&);
} // namespace wz3
