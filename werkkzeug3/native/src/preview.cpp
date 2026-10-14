// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " PROJECT.k OPERATOR_NAME_OR_ID OUTPUT.ppm [--font-file FONT] [--font-metrics JSON] [--alpha]\n";
    return 2;
  }
  try {
    wz3::TextureOptions options;
    std::filesystem::path metrics;
    bool alpha = false;
    for (int i = 4; i < argc; ++i) {
      const std::string_view flag(argv[i]);
      if (flag == "--alpha") alpha = true;
      else if (flag == "--font-file" && i + 1 < argc) options.font_file = argv[++i];
      else if (flag == "--font-metrics" && i + 1 < argc) metrics = argv[++i];
      else throw std::runtime_error("invalid or incomplete preview option: " + std::string(flag));
    }
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
    const auto texture = wz3::evaluate_texture(project, graph, id, options);
    for (const auto& font : texture->fonts) if (font)
      std::cerr << "Font page " << font->page << ": requested " << font->requested_family
                << ", using " << font->family << " from " << font->file << " (" << font->rasterizer << ")\n";
#if WZ3_GPU_PREVIEW
    auto pixels = wz3::render_texture(*texture);
#else
    auto pixels = wz3::rgba8(*texture);
#endif
    if (alpha) for (std::size_t i = 0; i < pixels.size(); i += 4) {
      pixels[i] = pixels[i+1] = pixels[i+2] = pixels[i+3]; pixels[i+3] = 255;
    }
    wz3::write_ppm(argv[3], texture->width, texture->height, pixels);
    if (!metrics.empty()) wz3::write_font_metrics(metrics, *texture);
    std::cout << "Rendered operator " << id << " (" << project.operators[id].name << "), "
              << texture->width << 'x' << texture->height << " to " << argv[3] << '\n';
  } catch (const std::exception& error) {
    std::cerr << "texture preview: " << error.what() << '\n';
    return 1;
  }
}
