// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
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
  std::optional<std::size_t> effect;
};
struct SceneDrawList {
  std::map<const Mesh*,std::vector<SceneMeshBatch>> meshes;
  std::vector<SceneDrawJob> jobs;
};
// Engine_::BuildPaintJobs/SortPaintJobs for ordinary mesh passes and effects.
// Lighting inserts, per-light jobs and implicit default materials need the
// light renderer; reject them rather than silently dropping their work.
SceneDrawList scene_unlit_jobs(const SceneFrame&);
} // namespace wz3
