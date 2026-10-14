// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <algorithm>
#include <stdexcept>

namespace wz3 {
void draw_cached_text(Texture& texture, std::span<const std::uint8_t> blob, std::uint32_t color, unsigned flags) {
  if (!(flags & 0x80) || blob.empty())
    throw std::runtime_error("Text requires native font rasterization: no enabled cached mask");
  if (flags & 0x70) throw std::runtime_error("cached font atlas metrics are not implemented");
  const auto word = [&](std::size_t i) { return unsigned(blob[i]) | (unsigned(blob[i+1]) << 8); };
  // Theta contains v2 masks with the same 8-byte header and 3-bit coverage
  // layout as v3. Use the saved glyphs rather than substituting host fonts.
  if (blob.size() < 8 || (blob[0] != 2 && blob[0] != 3) || blob[1] != 0 ||
      word(2) != texture.width || word(4) != texture.height ||
      blob.size() - 8 != texture.pixels.size())
    throw std::runtime_error("invalid cached Text mask header/dimensions");
  const auto mask = blob.subspan(8);
  if (std::any_of(mask.begin(), mask.end(), [](auto v) { return v > 7; }))
    throw std::runtime_error("invalid cached Text coverage");
  const auto overlay = color64(color);
  for (std::size_t i = 0; i < mask.size(); ++i) {
    const unsigned value = (mask[i] << 5) | (mask[i] << 2) | (mask[i] >> 1);
    texture.pixels[i] = fade64(texture.pixels[i], overlay, value | (value << 8));
  }
}
} // namespace wz3
