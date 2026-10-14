// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/mesh.hpp"

namespace wz3 {
constexpr std::uint8_t crease_normal = 2, crease_tangent = 4, crease_uv = 32;
inline std::uint32_t mesh_point(const Mesh& mesh, std::uint32_t v) {
  return mesh.vertices[v].point == mesh_no_point ? v : mesh.vertices[v].point;
}
// Corner IDs are face*4+corner; boundaries have no opposite.
std::vector<std::uint32_t> mesh_opposites(const Mesh&);
void crease_mesh_seams(Mesh&, std::uint8_t face_boundary_flags);
void smooth_mesh_angle(Mesh&, float angle);
} // namespace wz3
