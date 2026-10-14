// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Cached mask and letter metrics follow Bitmap_Text in ../genbitmap.cpp.
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace wz3 {
void draw_cached_text(Texture& texture, std::span<const std::uint8_t> blob, std::uint32_t color,
                      unsigned flags, float inner_spacing, std::string_view family) {
  if (!(flags & 0x80) || blob.empty())
    throw std::runtime_error("Text requires native font rasterization: no enabled cached mask");
  const auto page = (flags & 0x70) >> 4;
  const std::size_t header = 8 + (page ? 256 * 10 : 0);
  const auto word = [&](std::size_t i) { return unsigned(blob[i]) | (unsigned(blob[i+1]) << 8); };
  // Theta's editor project has v2 paragraph masks with the same header and
  // 3-bit coverage as v3. Font metrics have been verified only for v3 blobs.
  if (blob.size() < header || (blob[0] != 3 && (page || blob[0] != 2)) ||
      blob[1] != (page != 0) || !texture.width || !texture.height ||
      texture.width > 4096 || texture.height > 4096 ||
      texture.pixels.size() != std::size_t(texture.width) * texture.height ||
      word(2) != texture.width || word(4) != texture.height ||
      blob.size() - header != texture.pixels.size())
    throw std::runtime_error("invalid cached Text mask header/dimensions");
  const auto mask = blob.subspan(header);
  if (std::any_of(mask.begin(), mask.end(), [](auto v) { return v > 7; }))
    throw std::runtime_error("invalid cached Text coverage");
  std::shared_ptr<FontAtlas> atlas;
  if (page) {
    const auto xs = int(texture.width * 4), ys = int(texture.height * 4);
    if (!std::isfinite(inner_spacing) || inner_spacing < 0 || inner_spacing > 1 || !word(6))
      throw std::runtime_error("invalid cached font spacing/height");
    const int inner = inner_spacing * xs, height = word(6);
    atlas = std::make_shared<FontAtlas>();
    atlas->page = page;
    atlas->requested_family = atlas->family = family;
    atlas->rasterizer = "Werkkzeug3 cached Text v3";
    for (unsigned i = 0; i < 256; ++i) {
      const auto at = 8 + i * 10;
      const int x = word(at), y = word(at + 2);
      const auto signed_word = [&](std::size_t offset) {
        return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word(offset)));
      };
      const int w0 = signed_word(at + 4), w1 = signed_word(at + 6), w2 = signed_word(at + 8);
      if (w1 < 0) throw std::runtime_error("negative cached glyph ink width");
      // Keep signed bearings and all 256 entries, including padding/height for
      // absent glyphs. Do not clamp UVs: the release's Droid atlas extends just
      // beyond its bottom edge. Texture sampling, not this decoder, clips it.
      atlas->glyphs[i] = {{float(x) / xs, float(y) / ys,
                          float(x + w1 + inner * 2) / xs, float(y + height + inner * 2) / ys},
                         float(w0 - inner) / xs, float(w0 + w1 + w2 + inner * 2) / xs,
                         bool(x || y || w0 || w1 || w2)};
    }
  }
  const auto overlay = color64(color);
  for (std::size_t i = 0; i < mask.size(); ++i) {
    const unsigned value = (mask[i] << 5) | (mask[i] << 2) | (mask[i] >> 1);
    texture.pixels[i] = fade64(texture.pixels[i], overlay, value | (value << 8));
  }
  if (atlas) texture.fonts[page] = std::move(atlas);
}
} // namespace wz3
