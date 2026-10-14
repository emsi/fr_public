// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/animation.hpp"
#include "wz3/mesh.hpp"
#include "wz3/timeline.hpp"
#include "wz3/material.hpp"
#include "wz3/texture.hpp"

namespace wz3 {
// Column-major matrices. combine(parent,local) applies local first.
using SceneMatrix=std::array<float,16>;
SceneMatrix scene_identity();
SceneMatrix scene_srt(std::span<const std::uint32_t> parameters);
SceneMatrix scene_combine(const SceneMatrix& parent,const SceneMatrix& local);
Vec3 scene_point(const SceneMatrix&,const Vec3&);
struct SceneCamera {
  SceneMatrix world=scene_identity();
  float near_clip=.125f,far_clip=4096,center_x{},center_y{},zoom_x=1,zoom_y=1;
  std::uint32_t fog_color=0xff000000;
  float fog_start{},fog_end=4096;
};
SceneMatrix scene_view_projection(const SceneCamera&,unsigned width,unsigned height);
struct SceneInstance {
  std::size_t operator_id{};
  std::shared_ptr<const Mesh> mesh;
  SceneMatrix world=scene_identity();
  float time{};
  std::int32_t pass_adjust{};
};
struct SceneLight {
  std::size_t operator_id{};
  Vec3 position{},direction{};
  std::uint32_t flags{},color{};
  float amplify{},range{};
};
struct SceneEffectVertex {
  Vec3 position{};
  std::array<float,2> uv{};
  std::uint32_t color{};
};
struct SceneEffectDraw {
  std::size_t operator_id{};
  std::uint32_t sort_key{};
  MaterialShader shader;
  // Four vertices per source quad, in perimeter order.
  std::vector<SceneEffectVertex> vertices;
  bool normalized{};
  // Retain font pixels with the metrics that generated the glyph geometry.
  std::map<std::size_t,std::shared_ptr<const Texture>> textures;
};
struct SceneFrame {
  std::size_t viewport_id{},visited_operators{};
  SceneCamera camera;
  std::uint32_t viewport_flags{},background=0xff000000,ambient{};
  std::vector<SceneInstance> instances;
  std::vector<SceneLight> lights;
  std::vector<SceneEffectDraw> effects;
  std::map<std::size_t,Material> materials;
};
// Reuse a player across samples: static geometry and animation programs stay
// cached, while transforms/variables are evaluated for every instance and frame.
class ScenePlayer {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  ScenePlayer(const Project&,const Graph&,const TextureOptions& = {}); // References must outlive the player.
  ~ScenePlayer();
  ScenePlayer(const ScenePlayer&)=delete;
  ScenePlayer& operator=(const ScenePlayer&)=delete;
  SceneFrame viewport(std::size_t id,AnimationEnvironment environment,float aspect,
                      const SceneMatrix& world=scene_identity(),EventTimeline* timeline=nullptr);
  // Dispatch a named Event/Time branch through its aliases to viewports. This
  // evaluates nested scene events too; IPP compositing is not bypassed.
  std::vector<SceneFrame> event_viewports(std::size_t id,EventTimeline&,float aspect);
  MaterialRuntime& materials();
  // Required for elapsed-time simulation; a fresh standalone viewport has delta 0.
  void begin_frame(std::int32_t milliseconds);
  void prepare_effect(std::size_t id,std::span<const std::uint32_t> parameters);

};
Bounds scene_bounds(const SceneFrame&);
void write_scene(const std::filesystem::path&,const SceneFrame&);
// Geometry validation pass with the saved animated camera; material shaders,
// light accumulation, fog and postprocessing are still separate porting stages.
std::vector<std::uint8_t> render_scene(const SceneFrame&,unsigned width,unsigned height);
} // namespace wz3
