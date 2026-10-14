// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/graph.hpp"
#include <memory>

namespace wz3 {
using Pixel = std::array<std::uint16_t, 4>; // RGBA lanes, normally 0..32767; normals use alpha 0xffff
struct GlyphMetric {
  std::array<float, 4> uv{};
  float pre_space{}, advance{};
  bool present{};
};
struct FontAtlas {
  unsigned page{};
  std::filesystem::path file;
  std::string requested_family, family, rasterizer;
  std::array<GlyphMetric, 256> glyphs{};
};
struct TextureOptions { std::filesystem::path font_file; };
struct FontLayout {
  float x{}, y{}, height{}, width{}, inner_spacing{};
  int outer_spacing{};
  unsigned page{};
  std::uint32_t color{};
  std::string characters, requested_family;
};
struct Texture {
  unsigned width{}, height{};
  std::uint32_t format = 1, mip_count = 0, mip_threshold = 0;
  std::vector<Pixel> pixels;
  std::array<std::shared_ptr<const FontAtlas>, 8> fonts{};
};
Pixel color64(std::uint32_t argb);
Pixel fade64(const Pixel& a, const Pixel& b, std::int32_t fade);
Pixel merge_pixel(const Pixel& base, const Pixel& source, unsigned mode);
Pixel color_pixel(const Pixel& base, const Pixel& color, unsigned mode);
void draw_dots(Texture&, std::uint32_t color0, std::uint32_t color1, std::uint32_t density, std::uint32_t seed);
// Filter destinations/scratch are separately allocated, power-of-two textures.
Pixel sample_bitmap(const Texture&, std::int32_t u, std::int32_t v, unsigned border);
void transform_bitmap(const Texture&, Texture& destination, float angle, float sx, float sy,
                      float tx, float ty, unsigned border);
void blur_bitmap(Texture&, Texture& scratch, unsigned flags, float sx, float sy, float amplitude);
void normal_bitmap(const Texture&, Texture& destination, float distance, unsigned mode);
void adjust_hscb(Texture&, float hue, float saturation, float contrast, float brightness);
void draw_cached_text(Texture&, std::span<const std::uint8_t> blob, std::uint32_t color, unsigned flags);
void generate_font_atlas(Texture&, const FontLayout&, const std::filesystem::path& font_file);
void write_font_metrics(const std::filesystem::path&, const Texture&);
std::shared_ptr<const Texture> evaluate_texture(const Project&, const Graph&, std::size_t id, const TextureOptions& = {});
std::vector<std::uint8_t> rgba8(const Texture& texture);
void write_ppm(const std::filesystem::path&, unsigned width, unsigned height, std::span<const std::uint8_t> rgba);
// Throws on context/shader/framebuffer failure or any readback pixel mismatch.
std::vector<std::uint8_t> render_texture(const Texture& texture);
} // namespace wz3
