// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/texture.hpp"
#include <memory>
#include <limits>

namespace wz3 {
namespace legacy {class GenMesh;}
namespace legacy::minimal {class GenMinMesh;}
using Vec3 = std::array<float, 3>;
constexpr std::uint32_t mesh_no_point = std::numeric_limits<std::uint32_t>::max();
// Wedges can have different UVs/masks while belonging to the same topological
// point. Unspecified points use their vertex index (useful for hand-built meshes).
struct MeshVertex {
  Vec3 position{}, uv{};
  std::uint8_t mask{};
  std::uint32_t point = mesh_no_point;
  Vec3 tangent{};
  std::array<float,4> color{};
};
struct MeshFace {
  std::array<std::uint32_t, 4> vertices{};
  unsigned count = 4;
  std::uint8_t mask{};
  std::optional<std::size_t> material;
  std::int32_t pass{};
  // Attribute crease bits on the outgoing edge of each corner, as in GenMesh.
  std::array<std::uint8_t, 4> crease{};
  bool casts_shadow=true;
  // Original material cluster identity, including clusters with equal bindings.
  std::uint32_t cluster{};
};
// Polygon view of the immutable indexed core used by the evaluator. Hand-built
// diagnostics can omit the core. Point identity comes from the appropriate
// kernel: GenMesh topology or MINMESH's archived bitwise weld map.
struct Mesh {
  std::vector<MeshVertex> vertices;
  std::vector<MeshFace> faces;
  std::shared_ptr<const legacy::GenMesh> kernel;
  std::shared_ptr<const legacy::minimal::GenMinMesh> min_kernel;
  // A posed mesh retains its immutable source identity for bounded GPU caches.
  std::shared_ptr<const Mesh> animation_source;
};
struct Bounds { Vec3 min{}, max{}; };
std::shared_ptr<const Mesh> evaluate_mesh(const Project&, const Graph&, std::size_t id,const TextureOptions& = {});
bool mesh_has_animation(const Mesh&);
std::shared_ptr<const Mesh> pose_mesh(const std::shared_ptr<const Mesh>&,float time);
// Retain validated immutable topology and shared skin weights across frames.
class MeshPoseCache {
  struct Impl;std::unique_ptr<Impl> impl;
public:
  MeshPoseCache();~MeshPoseCache();
  std::shared_ptr<const Mesh> pose(const std::shared_ptr<const Mesh>&,float time);
};
Bounds mesh_bounds(const Mesh&);
void validate_mesh(const Mesh&);
std::size_t mesh_storage_bytes(const Mesh&);
using MeshNormals = std::vector<std::array<Vec3, 4>>;
MeshNormals mesh_normals(const Mesh&);
void write_obj(const std::filesystem::path&, const Mesh&);
// Diagnostic geometry rendering, independent of the saved material shaders.
std::vector<std::uint8_t> render_mesh(const Mesh&, unsigned width, unsigned height);
} // namespace wz3
