// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/graph.hpp"
#include <memory>

namespace wz3 {
using Vec3 = std::array<float, 3>;
struct MeshVertex { Vec3 position{}, uv{}; std::uint8_t mask{}; };
struct MeshFace {
  std::array<std::uint32_t, 4> vertices{};
  unsigned count = 4;
  std::uint8_t mask{};
  std::optional<std::size_t> material;
  std::int32_t pass{};
};
// Native polygon/UV representation. Original half-edge indices, crease groups,
// skinning and animation are not yet reproduced; dependent operators fail.
struct Mesh { std::vector<MeshVertex> vertices; std::vector<MeshFace> faces; };
struct Bounds { Vec3 min{}, max{}; };
std::shared_ptr<const Mesh> evaluate_mesh(const Project&, const Graph&, std::size_t id);
Bounds mesh_bounds(const Mesh&);
void validate_mesh(const Mesh&);
void write_obj(const std::filesystem::path&, const Mesh&);
// Diagnostic geometry rendering, independent of the saved material shaders.
std::vector<std::uint8_t> render_mesh(const Mesh&, unsigned width, unsigned height);
} // namespace wz3
