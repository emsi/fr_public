// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
namespace wz3 {
using ShadowPlane=std::array<float,4>;
struct ShadowEdge {std::array<std::uint32_t,2> vertices,planes;};
struct ShadowMesh {
  std::vector<Vec3> vertices;
  std::vector<ShadowPlane> planes;
  std::vector<ShadowEdge> edges;
  // Even indices address finite vertices; odd indices address their w=0 copy.
  std::vector<std::uint32_t> triangles,triangle_planes;
};
struct ShadowIndices {std::vector<std::uint32_t> indices;std::size_t silhouette_count{};};
// Kernel polygons retain the archive's topology, including original n-gons.
std::vector<std::uint8_t> shadow_face_mask(const Mesh&,const std::map<std::size_t,Material>&);
ShadowMesh prepare_shadow_mesh(const Mesh&,std::span<const std::uint8_t> mask);
// Refresh positions and planes for another pose of the same MINMESH.
// The caller retains the prepared face mask, topology and immutable source.
void update_shadow_mesh(ShadowMesh&,const Mesh& posed);
ShadowIndices shadow_indices(const ShadowMesh&,const Vec3& model_light);
std::vector<ShadowPlane> shadow_near_volume(const SceneCamera&,const Vec3& light,const std::array<float,4>& rectangle);
bool shadow_box_culled(const Bounds&,std::span<const ShadowPlane>);
} // namespace wz3
