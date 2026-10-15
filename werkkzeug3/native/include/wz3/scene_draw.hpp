// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
#include "wz3/shadow.hpp"
namespace wz3 {
struct SceneMeshBatch {
  std::optional<std::size_t> material;
  std::int32_t pass{};
  std::vector<std::uint32_t> faces;
};
std::vector<SceneMeshBatch> scene_mesh_batches(const Mesh&);
std::uint32_t mesh_vertex_color(const std::array<float,4>&);
struct SceneDrawJob {
  std::uint32_t sort_key{};
  std::size_t instance{},batch{},material_pass{};
  std::optional<std::size_t> effect,light;
  bool shadow_zfail{};
};
struct ScenePaintLight {
  SceneLight light;std::array<float,4> rectangle;
  std::array<ShadowPlane,5> shadow_frustum{};
  std::vector<ShadowPlane> near_volume{};
};
std::vector<ScenePaintLight> scene_lights(const SceneFrame&,unsigned width,unsigned height);
const Material& scene_default_material();
struct SceneDrawList {
  std::map<const Mesh*,std::vector<SceneMeshBatch>> meshes;
  std::vector<SceneDrawJob> jobs;
  std::vector<ScenePaintLight> lights;
  std::array<bool,256> inserts{};
  std::array<bool,256> material20_inserts{};
};
// Engine_::BuildPaintJobs/SortPaintJobs, including shared mesh shadow jobs.
SceneDrawList scene_draw_jobs(const SceneFrame&,unsigned width=1024,unsigned height=768);
} // namespace wz3
