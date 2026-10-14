// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Atlas packing and metric normalization follow Bitmap_Text in genbitmap.cpp.
// FreeType replaces GDI rasterization; metrics/pixels need reference comparison.
#include "wz3/texture.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace wz3 {
namespace {
void ft_check(FT_Error error, const char* action) {
  if (error) throw std::runtime_error(std::string(action) + " failed (FreeType " + std::to_string(error) + ")");
}
struct Font {
  FT_Library library{};
  FT_Face face{};
  explicit Font(const std::filesystem::path& path) {
    ft_check(FT_Init_FreeType(&library), "FT_Init_FreeType");
    try {
      ft_check(FT_New_Face(library, path.c_str(), 0, &face), "FT_New_Face");
      if (!FT_IS_SCALABLE(face)) throw std::runtime_error("font atlas needs a scalable font");
      ft_check(FT_Select_Charmap(face, FT_ENCODING_UNICODE), "FT_Select_Charmap");
    } catch (...) { if (face) FT_Done_Face(face); FT_Done_FreeType(library); throw; }
  }
  ~Font() { FT_Done_Face(face); FT_Done_FreeType(library); }
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;
};
int integer(float value) {
  if (!std::isfinite(value) || std::abs(value) > 1000000)
    throw std::runtime_error("font layout exceeds supported range");
  return static_cast<int>(value);
}
std::vector<unsigned> characters(std::string_view text) {
  std::vector<unsigned> result;
  for (std::size_t i = 0; i < text.size();) {
    const unsigned first = static_cast<unsigned char>(text[i++]);
    if (first < 32 || first >= 127) throw std::runtime_error("native font atlas currently supports printable ASCII");
    if (first == '-' && i < text.size() && text[i] == '-') ++i;
    result.push_back(first);
    if (i + 1 < text.size() && text[i] == '-' && text[i+1] != '-') {
      const unsigned last = static_cast<unsigned char>(text[i+1]);
      if (last < first || last >= 127) throw std::runtime_error("invalid font character range");
      for (unsigned c = first + 1; c <= last; ++c) result.push_back(c);
      i += 2;
    }
    if (result.size() > 4096) throw std::runtime_error("font character list exceeds limit");
  }
  return result;
}
}

void generate_font_atlas(Texture& texture, const FontLayout& layout, const std::filesystem::path& path) {
  if (path.empty()) throw std::runtime_error("uncached Text needs an explicit --font-file (saved family: " + layout.requested_family + ")");
  if (!layout.page || layout.page >= texture.fonts.size())
    throw std::runtime_error("uncached text paragraphs are not ported; expected a font atlas page");
  if (!texture.width || !texture.height || texture.width > 4096 || texture.height > 4096 ||
      texture.pixels.size() != std::size_t(texture.width) * texture.height ||
      texture.pixels.size() > 4 * 1024 * 1024)
    throw std::runtime_error("supersampled font mask exceeds 64 MiB limit");
  if (layout.width != 0) throw std::runtime_error("GDI font width stretching is not ported");
  if (layout.outer_spacing < 0 || layout.outer_spacing > 1024 || layout.inner_spacing < 0)
    throw std::runtime_error("invalid font spacing");
  const int xs = texture.width * 4, ys = texture.height * 4;
  const auto height = integer(ys * layout.height), inner = integer(xs * layout.inner_spacing);
  const auto left = integer(xs * layout.x), top = integer(ys * layout.y);
  if (height < 1 || height > 4096 || left < 0 || top < 0 || left >= xs || top >= ys || inner > xs)
    throw std::runtime_error("invalid font size/origin");
  const auto glyphs = characters(layout.characters);
  Font font(path);
  ft_check(FT_Set_Pixel_Sizes(font.face, 0, height), "FT_Set_Pixel_Sizes");
  const auto ascent = int(font.face->size->metrics.ascender / 64);
  const auto line_height = int((font.face->size->metrics.ascender - font.face->size->metrics.descender) / 64);
  if (line_height <= 0 || line_height > 16384) throw std::runtime_error("invalid font vertical metrics");
  auto atlas = std::make_shared<FontAtlas>();
  atlas->page = layout.page; atlas->file = std::filesystem::absolute(path);
  atlas->requested_family = layout.requested_family;
  atlas->family = font.face->family_name ? font.face->family_name : "unknown";
  int major, minor, patch;
  FT_Library_Version(font.library, &major, &minor, &patch);
  atlas->rasterizer = "FreeType " + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
  std::vector<std::uint8_t> mask(std::size_t(xs) * ys);
  const auto spacing = (layout.outer_spacing + 2) * 4;
  int xp = left + spacing, yp = top + spacing;
  for (const auto code : glyphs) {
    const auto index = FT_Get_Char_Index(font.face, code);
    if (!index) throw std::runtime_error("font has no glyph for character " + std::to_string(code));
    ft_check(FT_Load_Glyph(font.face, index, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL), "FT_Load_Glyph");
    const auto glyph = font.face->glyph;
    const auto& bitmap = glyph->bitmap;
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.num_grays != 256 || bitmap.pitch < 0)
      throw std::runtime_error("unsupported FreeType bitmap representation");
    const auto w0 = glyph->bitmap_left, w1 = int(bitmap.width);
    const auto advance = int(glyph->advance.x / 64), w2 = advance - w0 - w1;
    if (xp + w1 + spacing + 2 * inner >= xs) {
      yp = ((yp + line_height + 2 * inner + 3) & ~3) + spacing;
      xp = left + spacing;
    }
    if (xp + w1 + spacing + 2 * inner >= xs || yp + line_height + spacing + 2 * inner >= ys)
      throw std::runtime_error("font atlas is too small for requested character set");
    auto& metric = atlas->glyphs[code];
    metric = {{float(xp) / xs, float(yp) / ys, float(xp + w1 + inner * 2) / xs,
               float(yp + line_height + inner * 2) / ys}, float(w0 - inner) / xs,
               float(w0 + w1 + w2 + inner * 2) / xs, true};
    // GDI's top-aligned print at xp+inner-w0 cancels the left bearing.
    const int draw_x = xp + inner, draw_y = yp + inner + ascent - glyph->bitmap_top;
    for (unsigned y = 0; y < bitmap.rows; ++y) for (unsigned x = 0; x < bitmap.width; ++x) {
      const int dx = draw_x + x, dy = draw_y + y;
      if (dx >= 0 && dy >= 0 && dx < xs && dy < ys) {
        auto& target = mask[std::size_t(dy) * xs + dx];
        target = std::max(target, bitmap.buffer[std::size_t(y) * bitmap.pitch + x]);
      }
    }
    xp = ((xp + w1 + 2 * inner + 3) & ~3) + spacing;
  }
  const auto color = color64(layout.color);
  for (unsigned y = 0; y < texture.height; ++y) for (unsigned x = 0; x < texture.width; ++x) {
    unsigned sum = 0;
    for (unsigned sy = 0; sy < 4; ++sy) for (unsigned sx = 0; sx < 4; ++sx)
      sum += mask[std::size_t(y * 4 + sy) * xs + x * 4 + sx];
    const unsigned coverage = sum >> 4;
    auto& p = texture.pixels[std::size_t(y) * texture.width + x];
    p = fade64(p, color, coverage * 257);
  }
  texture.fonts[layout.page] = std::move(atlas);
}

void write_font_metrics(const std::filesystem::path& path, const Texture& texture) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot open font metric output");
  out << std::setprecision(9) << "{\"width\":" << texture.width << ",\"height\":" << texture.height << ",\"pages\":[";
  bool first_page = true;
  for (const auto& atlas : texture.fonts) if (atlas) {
    if (!first_page) out << ',';
    first_page = false;
    out << "{\"page\":" << atlas->page << ",\"file\":" << json_string(atlas->file.string())
        << ",\"requested_family\":" << json_string(atlas->requested_family) << ",\"family\":" << json_string(atlas->family)
        << ",\"rasterizer\":" << json_string(atlas->rasterizer) << ",\"glyphs\":[";
    bool first_glyph = true;
    for (unsigned i = 0; i < atlas->glyphs.size(); ++i) {
      const auto& glyph = atlas->glyphs[i];
      if (!glyph.present) continue;
      if (!first_glyph) out << ',';
      first_glyph = false;
      out << "{\"code\":" << i << ",\"uv\":[" << glyph.uv[0] << ',' << glyph.uv[1] << ',' << glyph.uv[2] << ',' << glyph.uv[3]
          << "],\"pre_space\":" << glyph.pre_space << ",\"advance\":" << glyph.advance << '}';
    }
    out << "]}";
  }
  out << "]}\n";
  if (!out) throw std::runtime_error("cannot write font metrics");
}
} // namespace wz3
