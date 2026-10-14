// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/animation.hpp"
#include "wz3/graph.hpp"
#include <memory>
#include <ostream>

namespace wz3 {
enum class MaterialUsage {base,prelight,ambient,shadow,light,postlight,environment,other};
enum class MaterialProgram {static_mesh=1,shadow=2,sprites=3,thick_lines=4,instances=7};
struct MaterialShader {
  // sMaterial11's portable 48-word parameter layout: flags 0..2, sampler
  // flags 4..7, combiners 8..20, alpha 23, UV scales 24..27, colors 28..31,
  // specular power 32, SRT1 34..42 and SRT2 43..47.
  std::array<std::uint32_t,48> words{};
  std::array<std::optional<std::size_t>,4> textures;
  std::size_t source_operator{};
};
struct MaterialPass {
  MaterialShader shader;
  MaterialUsage usage=MaterialUsage::other;
  MaterialProgram program=MaterialProgram::static_mesh;
  std::int32_t render_pass{};
  float size=1,aspect=1;
};
struct Material {
  std::vector<MaterialPass> passes;
  bool material11_insert{};
};
// Owns the shared mutable shader objects used by Material/Add/Load/Store/Nop.
// A snapshot copies their current values so a later frame cannot change it.
class MaterialRuntime {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  MaterialRuntime(const Project&,const Graph&); // References must outlive runtime.
  ~MaterialRuntime();
  MaterialRuntime(const MaterialRuntime&)=delete;
  MaterialRuntime& operator=(const MaterialRuntime&)=delete;
  void precalculate(std::size_t id,AnimationEnvironment&);
  // Lazily initializes absent materials at beat zero. A root player should
  // precalculate with its ancestor scopes before executing the first frame.
  void execute(std::size_t id,AnimationEnvironment&);
  Material snapshot(std::size_t id);
  void reset();
};
void write_material(std::ostream&,const Material&);
// Translate the original unlit pixel combiner to GLES3; lighting/environment
// shaders require their own vertex inputs and are rejected here.
std::string material_fragment_shader(const MaterialShader&);
std::array<float,9> material_uv_matrix(const MaterialShader&,unsigned slot);
struct Texture;
std::vector<std::uint8_t> render_material_quad(const Material&,
  const std::map<std::size_t,std::shared_ptr<const Texture>>& textures,
  unsigned width,unsigned height,std::uint32_t background=0xff202020);
} // namespace wz3
