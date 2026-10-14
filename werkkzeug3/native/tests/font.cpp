// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
  bool rejected = false;
  try { action(); } catch (const std::runtime_error&) { rejected = true; }
  check(rejected, "invalid font request accepted");
}
}
int main(int argc, char** argv) {
  try {
    check(argc == 3, "expected project and font paths");
    const auto project = wz3::read_project(argv[1]);
    const auto graph = wz3::connect(project);
    const auto id = graph.names.at("font_bitmap");
    rejects([&] { wz3::evaluate_texture(project, graph, id); });
    const wz3::TextureOptions options{argv[2]};
    const auto texture = wz3::evaluate_texture(project, graph, id, options);
    check(texture->width == 512 && texture->height == 512, "Theta font dimensions");
    check(bool(texture->fonts[1]), "font metrics lost through Merge/Store");
    const auto& font = *texture->fonts[1];
    check(std::count_if(font.glyphs.begin(), font.glyphs.end(), [](auto& g) { return g.present; }) == 69,
          "Theta ASCII ranges did not expand to 69 glyphs");
    unsigned visible = 0;
    for (const auto& p : texture->pixels) {
      check(p[0] == 32767 && p[1] == 32767 && p[2] == 32767, "font RGB must remain white after alpha Merge");
      visible += p[3] > 0;
    }
    check(visible > 1000 && visible < texture->pixels.size() / 2, "font atlas has no useful alpha coverage");
    for (unsigned code = 0; code < font.glyphs.size(); ++code) {
      const auto& g = font.glyphs[code];
      if (!g.present) continue;
      check(g.uv[0] >= 0 && g.uv[1] >= 0 && g.uv[2] <= 1 && g.uv[3] <= 1 && g.advance > 0, "invalid glyph metrics");
      if (code == ' ') continue;
      bool ink = false;
      for (unsigned y = g.uv[1] * texture->height; y < g.uv[3] * texture->height; ++y)
        for (unsigned x = g.uv[0] * texture->width; x < g.uv[2] * texture->width; ++x)
          ink |= texture->pixels[y * texture->width + x][3] != 0;
      check(ink, "glyph UV rectangle does not contain ink");
    }
    wz3::Texture blank; blank.width = blank.height = 32; blank.pixels.resize(1024);
    wz3::FontLayout layout{0, 0, 0.25f, 0, 0, 2, 1, 0xffffffff, "a-z", "Arial"};
    rejects([&] { wz3::generate_font_atlas(blank, layout, argv[2]); }); // deliberately too small
    layout.height = 0;
    rejects([&] { wz3::generate_font_atlas(blank, layout, argv[2]); });
    layout.height = 0.125f; layout.characters = "z-a";
    rejects([&] { wz3::generate_font_atlas(blank, layout, argv[2]); });
    layout.characters = "a";
    rejects([&] { wz3::generate_font_atlas(blank, layout, "/nonexistent-font.ttf"); });
    // Explicit font selection must not alter the archived cached text.
    const auto cached_id = graph.names.at("tex.intro4");
    const auto cached = wz3::evaluate_texture(project, graph, cached_id);
    const auto explicit_font = wz3::evaluate_texture(project, graph, cached_id, options);
    check(cached->pixels == explicit_font->pixels, "explicit font changed a cached text mask");
    std::cout << "Theta font atlas: 69 glyphs, alpha coverage and retained metrics passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
