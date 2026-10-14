// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/graph.hpp"
#include <memory>

namespace wz3 {
using Pixel = std::array<std::uint16_t, 4>; // RGBA lanes, normally 0..32767; normals use alpha 0xffff
struct Texture {
  unsigned width{}, height{};
  std::uint32_t format = 1, mip_count = 0, mip_threshold = 0;
  std::vector<Pixel> pixels;
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
std::shared_ptr<const Texture> evaluate_texture(const Project&, const Graph&, std::size_t id);
std::vector<std::uint8_t> rgba8(const Texture& texture);
void write_ppm(const std::filesystem::path&, unsigned width, unsigned height, std::span<const std::uint8_t> rgba);
// Throws on context/shader/framebuffer failure or any readback pixel mismatch.
std::vector<std::uint8_t> render_texture(const Texture& texture);
} // namespace wz3
