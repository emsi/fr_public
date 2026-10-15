// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/animation.hpp"
#include "wz3/mesh.hpp"
#include "wz3/timeline.hpp"
#include "wz3/material.hpp"
#include "wz3/texture.hpp"
#include "wz3/glare.hpp"
#include "wz3/color_correction.hpp"
#include "wz3/water.hpp"

namespace wz3 {
// Column-major matrices. combine(parent,local) applies local first.
using SceneMatrix=std::array<float,16>;
using SceneMarkers=std::array<SceneMatrix,32>;
SceneMatrix scene_identity();
SceneMatrix scene_srt(std::span<const std::uint32_t> parameters);
SceneMatrix scene_combine(const SceneMatrix& parent,const SceneMatrix& local);
Vec3 scene_point(const SceneMatrix&,const Vec3&);
struct SceneCamera {
  SceneMatrix world=scene_identity();
  float near_clip=.125f,far_clip=4096,center_x{},center_y{},zoom_x=1,zoom_y=1;
  std::uint32_t fog_color=0xff000000;
  float fog_start{},fog_end=4096;
  bool released_projection{};
};
// Geometric clip matrix, including D3D-to-GL depth conversion. Culling uses it
// before the rasterizer's pixel-center adjustment.
SceneMatrix scene_view_projection(const SceneCamera&,unsigned width,unsigned height);
SceneMatrix scene_raster_projection(const SceneCamera&,unsigned width,unsigned height);
struct SceneInstance {
  std::size_t operator_id{};
  std::shared_ptr<const Mesh> mesh;
  SceneMatrix world=scene_identity();
  float time{};
  std::int32_t pass_adjust{};
  // EngMesh owns one persistent instance array, shared by all its paint jobs.
  // A frame snapshots the final array after scene traversal. Only passes with
  // MaterialProgram::instances use it; static and shadow passes keep the mesh.
  std::shared_ptr<const std::vector<SceneMatrix>> particle_matrices{};
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
struct SceneRibbon {
  std::array<Vec3,32> points{};
  float thickness{};
  int ripped=-1;
};
struct SceneEffectDraw {
  std::size_t operator_id{};
  std::uint32_t sort_key{};
  MaterialShader shader;
  // Four vertices per source quad, in perimeter order.
  std::vector<SceneEffectVertex> vertices;
  bool normalized{};
  // ChainLine reads the preceding material's model transform at draw time.
  std::optional<SceneRibbon> ribbon;
  SceneMatrix material_model=scene_identity();
  bool material_on_empty{};
  // Print and Water use the camera left in KEnvironment::CurrentCam.
  std::optional<SceneMatrix> camera_world;
  // Retain font pixels with the metrics that generated the glyph geometry.
  std::map<std::size_t,std::shared_ptr<const Texture>> textures;
  std::optional<GlareEffect> glare;
  std::optional<ColorCorrectionEffect> color_correction;
  std::optional<WaterEffect> water;
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
// Immutable geometry/font data may survive a loading pass. Simulation, RNG,
// material animation and event state always belong to each separate player.
class SceneAssets {
  const Project& project;const Graph& graph;TextureOptions options;
  std::map<std::size_t,std::shared_ptr<const Mesh>> meshes;
  std::map<std::size_t,std::shared_ptr<const Texture>> fonts;
  std::size_t mesh_bytes{},font_bytes{};
public:
  struct Stats {std::size_t meshes{},fonts{},mesh_bytes{},font_bytes{};};
  SceneAssets(const Project&,const Graph&,const TextureOptions& = {});
  bool matches(const Project&,const Graph&,const TextureOptions&) const;
  std::shared_ptr<const Mesh> mesh(std::size_t);
  std::shared_ptr<const Texture> font(std::size_t);
  Stats stats() const {return {meshes.size(),fonts.size(),mesh_bytes,font_bytes};}
};
// Reuse a player across samples: static geometry and animation programs stay
// cached, while transforms/variables are evaluated for every instance and frame.
class ScenePlayer {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  ScenePlayer(const Project&,const Graph&,const TextureOptions& = {},std::shared_ptr<SceneAssets> = {}); // References must outlive the player/cache.
  ~ScenePlayer();
  ScenePlayer(const ScenePlayer&)=delete;
  ScenePlayer& operator=(const ScenePlayer&)=delete;
  SceneFrame viewport(std::size_t id,AnimationEnvironment environment,float aspect,
                      const SceneMatrix& world=scene_identity(),EventTimeline* timeline=nullptr);
  // Dispatch a named Event/Time branch through its aliases to viewports. This
  // evaluates nested scene events too; IPP compositing is not bypassed.
  std::vector<SceneFrame> event_viewports(std::size_t id,EventTimeline&,float aspect);
  MaterialRuntime& materials();
  // KEnvironment markers survive frame and viewport boundaries until a new
  // player/view is initialized. Deferred effects read the latest transforms.
  const SceneMarkers& markers() const;
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
