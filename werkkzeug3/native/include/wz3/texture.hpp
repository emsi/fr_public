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
Pixel mask_pixel(const Pixel& mask,const Pixel& a,const Pixel& b,unsigned mode);
Pixel range_pixel(const Pixel&,const Pixel& low,const Pixel& high,unsigned mode);
void draw_dots(Texture&, std::uint32_t color0, std::uint32_t color1, std::uint32_t density, std::uint32_t seed);
// Filter destinations/scratch are separately allocated, power-of-two textures.
Pixel sample_bitmap(const Texture&, std::int32_t u, std::int32_t v, unsigned border);
void transform_bitmap(const Texture&, Texture& destination, float angle, float sx, float sy,
                      float tx, float ty, unsigned border);
void blur_bitmap(Texture&, Texture& scratch, unsigned flags, float sx, float sy, float amplitude);
void normal_bitmap(const Texture&, Texture& destination, float distance, unsigned mode);
void adjust_hscb(Texture&, float hue, float saturation, float contrast, float brightness);
// Bitmap_Bump's 14 saved words; an absent normal map is the Bitmap_Light path.
void bump_bitmap(Texture&, const Texture* normals, std::span<const std::uint32_t> parameters);
Pixel add_scale64(const Pixel& base,const Pixel& color,std::int32_t fade);
// Saved editor-project Bitmap_Perlin (!sINTRO), including fixed-point rows.
void perlin_bitmap(Texture&,std::span<const std::uint32_t> parameters);
void balance_bitmap(Texture&,std::span<const std::uint32_t> parameters);
void distort_bitmap(const Texture& image,const Texture& displacement,Texture& destination,float amount,unsigned border);
void sharpen_bitmap(Texture& blurred,const Texture& original,float amplitude);
void bricks_bitmap(Texture&,std::span<const std::uint32_t> parameters);
// Saved editor-project (!sINTRO) Cell tiling and stable point ordering.
void cell_bitmap(Texture&,std::span<const std::uint32_t> parameters);
void bulge_bitmap(const Texture& source,Texture& destination,float warp);
void draw_cached_text(Texture&, std::span<const std::uint8_t> blob, std::uint32_t color, unsigned flags,
                      float inner_spacing = 0, std::string_view family = {});
void generate_font_atlas(Texture&, const FontLayout&, const std::filesystem::path& font_file);
void write_font_metrics(const std::filesystem::path&, const Texture&);
// Recipe-local allocations (including scratch), not GPU storage or font tables.
struct TextureEvaluationStats {std::size_t operators{},allocations{},peak_pixel_bytes{};};
std::shared_ptr<const Texture> evaluate_texture(const Project&, const Graph&, std::size_t id, const TextureOptions& = {},TextureEvaluationStats* = nullptr);
std::vector<std::uint8_t> rgba8(const Texture& texture);
struct ColorTextureLevel {unsigned width,height;std::vector<std::uint8_t> pixels;};
// Original ARGB8 upload: reduce 15-bit lanes before packing each mip to bytes.
std::vector<ColorTextureLevel> color_texture_levels(const Texture&);
void write_ppm(const std::filesystem::path&, unsigned width, unsigned height, std::span<const std::uint8_t> rgba);
// Throws on context/shader/framebuffer failure or any readback pixel mismatch.
std::vector<std::uint8_t> render_texture(const Texture& texture);
} // namespace wz3
