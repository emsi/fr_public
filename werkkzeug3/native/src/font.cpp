// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Atlas packing and metric normalization follow Bitmap_Text in genbitmap.cpp.
// FreeType replaces GDI rasterization; metrics/pixels need reference comparison.
#include "wz3/texture.hpp"
#include "text_layout.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <algorithm>
#include <cmath>
#include <cfenv>
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

namespace detail {
std::vector<TextLine> paragraph_lines(const FontLayout& layout,unsigned width,unsigned height,int line_height,
                                    const std::function<int(std::string_view)>& measure) {
  if(!width || !height || width>4096 || height>4096 || line_height<1 || line_height>16384 ||
     layout.characters.size()>16383 || layout.characters.find('\0')!=std::string::npos ||
     layout.outer_spacing < -1024 || layout.outer_spacing>1024)
    throw std::runtime_error("invalid text paragraph layout");
  const int spacing=(layout.outer_spacing+2)*4;
  const int x=integer(layout.x*(width*4)+spacing),skip=integer(layout.line_skip*line_height);
  int y=integer(layout.y*(height*4)+spacing);
  if(layout.flags&2)y-=line_height*(std::count(layout.characters.begin(),layout.characters.end(),'\n')+1)/2;
  std::vector<TextLine> lines;std::string_view rest=layout.characters;
  while(!rest.empty()) {
    auto end=rest.find('\n');if(end==std::string_view::npos)end=rest.size();
    const auto line=rest.substr(0,end);
    if(!line.empty())lines.push_back({x-((layout.flags&1) ? measure(line)/2 : 0),y,line});
    rest.remove_prefix(end);
    if(!rest.empty()){y=integer(double(y)+skip);rest.remove_prefix(1);}
  }
  return lines;
}
void composite_text_mask(Texture& texture,std::span<const std::uint8_t> mask,std::uint32_t argb) {
  if(mask.size()!=std::size_t(texture.width)*texture.height*16 || texture.pixels.size()!=std::size_t(texture.width)*texture.height)
    throw std::runtime_error("invalid supersampled text mask");
  const auto color=color64(argb);const auto stride=texture.width*4;
  for(unsigned y=0;y<texture.height;++y)for(unsigned x=0;x<texture.width;++x) {
    unsigned sum=0;
    for(unsigned dy=0;dy<4;++dy)for(unsigned dx=0;dx<4;++dx)sum+=mask[std::size_t(y*4+dy)*stride+x*4+dx];
    auto& pixel=texture.pixels[std::size_t(y)*texture.width+x];pixel=fade64(pixel,color,(sum>>4)*257);
  }
}
} // namespace detail

void draw_text_paragraph(Texture& texture,const FontLayout& layout,const std::filesystem::path& path) {
  if(path.empty())throw std::runtime_error("uncached Text needs an explicit --font-file (saved family: "+layout.requested_family+")");
  if(layout.page || !texture.width || !texture.height || texture.width>4096 || texture.height>4096 ||
     texture.pixels.size()!=std::size_t(texture.width)*texture.height || texture.pixels.size()>4*1024*1024)
    throw std::runtime_error("invalid text paragraph texture/page or mask exceeds 64 MiB limit");
  struct Nearest {std::fenv_t saved;Nearest(){std::fegetenv(&saved);std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetenv(&saved);}} rounding;
  const int xs=texture.width*4,ys=texture.height*4;
  const int height=integer(ys*layout.height),width=integer(xs*layout.width);
  if(height<1 || height>4096 || width<0 || width>4096)throw std::runtime_error("invalid paragraph font size");
  Font font(path);
  if(width) {
    // CreateFont's width is an average character width, not the em width.
    // Scale the font's OS/2 average to the requested width at unchanged height.
    const auto* os2=static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(font.face,ft_sfnt_os2));
    if(!os2 || os2->xAvgCharWidth<=0)throw std::runtime_error("explicit font width needs an OS/2 average character width");
    FT_Size_RequestRec request{FT_SIZE_REQUEST_TYPE_NOMINAL,FT_MulDiv(width*64,font.face->units_per_EM,os2->xAvgCharWidth),height*64,0,0};
    if(request.width>16384*64)throw std::runtime_error("paragraph font horizontal scale exceeds limit");
    ft_check(FT_Request_Size(font.face,&request),"FT_Request_Size");
  }else ft_check(FT_Set_Pixel_Sizes(font.face,0,height),"FT_Set_Pixel_Sizes");
  const int ascent=font.face->size->metrics.ascender/64;
  const int line_height=(font.face->size->metrics.ascender-font.face->size->metrics.descender)/64;
  // Saved sChar strings are Windows bytes, not UTF-8. Debris uses CP1252
  // punctuation; codepoints 0xa0..0xff have the same Unicode values.
  constexpr unsigned cp1252[]={0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
    0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
  struct Glyph {int advance,left,top,width,height;std::vector<std::uint8_t> pixels;};
  std::array<std::optional<Glyph>,256> glyphs;
  const auto glyph=[&](unsigned char code)->const Glyph& {
    if(glyphs[code])return *glyphs[code];
    const auto unicode=code>=128 && code<160 ? cp1252[code-128] : unsigned(code);
    const auto index=FT_Get_Char_Index(font.face,unicode);
    if(!index)throw std::runtime_error("font has no paragraph glyph for Windows byte "+std::to_string(code));
    ft_check(FT_Load_Glyph(font.face,index,FT_LOAD_RENDER|FT_LOAD_NO_BITMAP|FT_LOAD_TARGET_NORMAL),"FT_Load_Glyph");
    const auto g=font.face->glyph;const auto& b=g->bitmap;
    if(b.pixel_mode!=FT_PIXEL_MODE_GRAY || b.num_grays!=256 || b.pitch<0)throw std::runtime_error("unsupported paragraph glyph bitmap");
    Glyph result{int(g->advance.x/64),g->bitmap_left,g->bitmap_top,int(b.width),int(b.rows),{}};
    result.pixels.resize(std::size_t(b.width)*b.rows);
    for(unsigned y=0;y<b.rows;++y)std::copy_n(b.buffer+std::size_t(y)*b.pitch,b.width,result.pixels.begin()+std::size_t(y)*b.width);
    glyphs[code]=std::move(result);return *glyphs[code];
  };
  const auto measure=[&](std::string_view text){int advance=0;for(unsigned char c:text)advance+=glyph(c).advance;return advance;};
  const auto lines=detail::paragraph_lines(layout,texture.width,texture.height,line_height,measure);
  std::vector<std::uint8_t> mask(std::size_t(xs)*ys);
  for(const auto& line:lines) {
    int pen=line.x;
    for(unsigned char c:line.text) {
      const auto& g=glyph(c);const int left=pen+g.left,top=line.y+ascent-g.top;
      for(int y=std::max(0,-top);y<std::min(g.height,ys-top);++y)for(int x=std::max(0,-left);x<std::min(g.width,xs-left);++x) {
        auto& dest=mask[std::size_t(top+y)*xs+left+x];const auto coverage=g.pixels[std::size_t(y)*g.width+x];
        dest=coverage+(unsigned(dest)*(255-coverage)+127)/255;
      }
      pen+=g.advance;
    }
  }
  detail::composite_text_mask(texture,mask,layout.color);
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
  // The original final metric loop covers all 256 entries, including characters
  // absent from the requested set. Those entries retain zero glyph dimensions
  // but still receive the common line height and inner spacing.
  for(auto& glyph:atlas->glyphs)glyph={{0,0,float(inner*2)/xs,float(line_height+inner*2)/ys},-float(inner)/xs,float(inner*2)/xs,false};
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
