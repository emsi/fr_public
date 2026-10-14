// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/mesh.hpp"
#include "wz3/texture.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " PROJECT.k OPERATOR_NAME_OR_ID OUTPUT\n";
    return 2;
  }
  try {
    const auto project = wz3::read_project(argv[1]);
    const auto graph = wz3::connect(project);
    const std::string_view selector(argv[2]);
    std::size_t id{};
    if (const auto found = graph.names.find(selector); found != graph.names.end()) id = found->second;
    else {
      const auto [end,error] = std::from_chars(selector.data(),selector.data()+selector.size(),id);
      if (error != std::errc{} || end != selector.data()+selector.size()) throw std::runtime_error("no mesh named " + std::string(selector));
    }
    const auto mesh = wz3::evaluate_mesh(project,graph,id);
#if WZ3_GPU_PREVIEW
    const auto pixels = wz3::render_mesh(*mesh,768,768);
    wz3::write_ppm(argv[3],768,768,pixels);
#else
    wz3::write_obj(argv[3],*mesh);
#endif
    const auto bounds = wz3::mesh_bounds(*mesh);
    std::cout << "Operator " << id << ": " << mesh->vertices.size() << " vertices, " << mesh->faces.size() << " polygons\nBounds: [";
    for (unsigned c = 0; c < 3; ++c) std::cout << (c ? ", " : "") << bounds.min[c];
    std::cout << "] .. [";
    for (unsigned c = 0; c < 3; ++c) std::cout << (c ? ", " : "") << bounds.max[c];
    std::cout << "]\nGeometry preview: material shaders, original smoothing and scene animation are not evaluated.\n";
  } catch (const std::exception& error) { std::cerr << "mesh preview: " << error.what() << '\n'; return 1; }
}
