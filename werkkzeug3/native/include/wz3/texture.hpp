// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/graph.hpp"
#include <memory>

namespace wz3 {
using Pixel = std::array<std::uint16_t, 4>; // RGBA, original 0..32767 channels
struct Texture {
  unsigned width{}, height{};
  std::vector<Pixel> pixels;
};
Pixel color64(std::uint32_t argb);
Pixel fade64(const Pixel& a, const Pixel& b, std::int32_t fade);
std::shared_ptr<const Texture> evaluate_texture(const Project&, const Graph&, std::size_t id);
std::vector<std::uint8_t> rgba8(const Texture& texture);
void write_ppm(const std::filesystem::path&, unsigned width, unsigned height, std::span<const std::uint8_t> rgba);
// Throws on context/shader/framebuffer failure or any readback pixel mismatch.
std::vector<std::uint8_t> render_texture(const Texture& texture);
} // namespace wz3
