// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " PROJECT.k OPERATOR_NAME_OR_ID OUTPUT.ppm\n";
    return 2;
  }
  try {
    const auto project = wz3::read_project(argv[1]);
    const auto graph = wz3::connect(project);
    const std::string_view selector(argv[2]);
    std::size_t id = 0;
    if (const auto found = graph.names.find(selector); found != graph.names.end()) id = found->second;
    else {
      const auto [end, error] = std::from_chars(selector.data(), selector.data() + selector.size(), id);
      if (error != std::errc{} || end != selector.data() + selector.size())
        throw std::runtime_error("no operator named " + std::string(selector));
    }
    const auto texture = wz3::evaluate_texture(project, graph, id);
#if WZ3_GPU_PREVIEW
    const auto pixels = wz3::render_texture(*texture);
#else
    const auto pixels = wz3::rgba8(*texture);
#endif
    wz3::write_ppm(argv[3], texture->width, texture->height, pixels);
    std::cout << "Rendered operator " << id << " (" << project.operators[id].name << "), "
              << texture->width << 'x' << texture->height << " to " << argv[3] << '\n';
  } catch (const std::exception& error) {
    std::cerr << "texture preview: " << error.what() << '\n';
    return 1;
  }
}
