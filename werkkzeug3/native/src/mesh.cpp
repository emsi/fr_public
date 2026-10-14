// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/mesh.hpp"
#include "mesh_kernel.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace wz3 {
namespace {constexpr std::size_t element_limit=1<<20;}
Bounds mesh_bounds(const Mesh& mesh) {
  validate_mesh(mesh);
  if (mesh.faces.empty()) throw std::runtime_error("empty mesh has no bounds");
  const auto first=mesh.vertices[mesh.faces[0].vertices[0]].position;
  Bounds result{first,first};
  // Subdivide keeps obsolete attribute slots; only live faces define bounds.
  for (const auto& face:mesh.faces) for(unsigned i=0;i<face.count;++i) for (unsigned c = 0; c < 3; ++c) {
    const auto& v=mesh.vertices[face.vertices[i]];
    result.min[c] = std::min(result.min[c],v.position[c]); result.max[c] = std::max(result.max[c],v.position[c]);
  }
  return result;
}
void validate_mesh(const Mesh& mesh) {
  if (mesh.vertices.size() > element_limit || mesh.faces.size() > element_limit) throw std::runtime_error("mesh resource limit exceeded");
  for (const auto& v : mesh.vertices) for (unsigned c = 0; c < 3; ++c)
    if (!std::isfinite(v.position[c]) || !std::isfinite(v.uv[c])) throw std::runtime_error("non-finite mesh geometry");
  for (const auto& v:mesh.vertices) if (v.point!=mesh_no_point && v.point>=mesh.vertices.size())
    throw std::runtime_error("mesh topological point out of bounds");
  for (const auto& f : mesh.faces) {
    if (f.count < 3 || f.count > 4) throw std::runtime_error("invalid mesh polygon");
    for (unsigned i = 0; i < f.count; ++i) if (f.vertices[i] >= mesh.vertices.size()) throw std::runtime_error("mesh index out of bounds");
  }
}
std::shared_ptr<const Mesh> evaluate_mesh(const Project& project, const Graph& graph, std::size_t id) {
  return legacy::evaluate_kernel(project,graph,id);
}
void write_obj(const std::filesystem::path& path, const Mesh& mesh) {
  validate_mesh(mesh);
  const auto normals=mesh_normals(mesh);
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot open mesh output");
  out << "# Native Theta geometry preview; material operator IDs are metadata.\n" << std::setprecision(9);
  for (const auto& v : mesh.vertices) out << "v " << v.position[0] << ' ' << v.position[1] << ' ' << v.position[2] << '\n';
  for (const auto& v : mesh.vertices) out << "vt " << v.uv[0] << ' ' << v.uv[1] << ' ' << v.uv[2] << '\n';
  for (std::size_t f=0;f<mesh.faces.size();++f) for (unsigned c=0;c<mesh.faces[f].count;++c) {
    const auto& n=normals[f][c]; out << "vn " << n[0] << ' ' << n[1] << ' ' << n[2] << '\n';
  }
  std::optional<std::size_t> previous; std::int32_t pass = 0; bool first = true;
  std::size_t normal_index=1;
  for (const auto& f : mesh.faces) {
    if (first || previous != f.material || pass != f.pass) {
      out << "g material_" << (f.material ? std::to_string(*f.material) : "default") << "_pass_" << f.pass << '\n';
      first = false; previous = f.material; pass = f.pass;
    }
    out << 'f';
    for (unsigned i = 0; i < f.count; ++i) out << ' ' << f.vertices[i]+1 << '/' << f.vertices[i]+1 << '/' << normal_index++;
    out << '\n';
  }
  if (!out) throw std::runtime_error("cannot write mesh output");
}
} // namespace wz3
