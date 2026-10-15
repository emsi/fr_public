// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Lane operations preserve Bitmap_Inner's signed/unsigned MMX distinctions.
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace wz3 {
namespace {
int signed_lane(std::uint16_t value) { return std::bit_cast<std::int16_t>(value); }
std::uint16_t saturate(int value) { return static_cast<std::uint16_t>(std::clamp(value, -32768, 32767)); }
std::uint16_t multiply(std::uint16_t a, std::uint16_t b, unsigned shift = 1) {
  return static_cast<std::uint16_t>(((signed_lane(a) * signed_lane(b)) >> 16) * (1 << shift));
}
unsigned gray(const Pixel& p) {
  // The original stores BGRA and averages B+G and R+G separately.
  const auto bg = std::uint16_t(p[2] + p[1]) >> 1;
  const auto rg = std::uint16_t(p[0] + p[1]) >> 1;
  return std::uint16_t(bg + rg) >> 1;
}
class Random {
public:
  explicit Random(std::uint32_t seed) {
    const auto product = seed * 121u;
    // Signed idiv truncates toward zero after the original 32-bit multiply.
    const auto mixed = seed + seed * 17u + product + std::uint32_t(std::bit_cast<std::int32_t>(product) / 17);
    state_ = mixed;
    for (unsigned i = 0; i < 4; ++i) { next(); if (i != 3) state_ ^= mixed; }
  }
  std::uint32_t next() {
    const auto first = state_ * 0x343fdu + 0x269ec3u;
    state_ = first * 0x343fdu + 0x269ec3u;
    return ((state_ >> 10) & 0xffffu) | ((first << 6) & 0xffff0000u);
  }
private:
  std::uint32_t state_{};
};
}

Pixel merge_pixel(const Pixel& base, const Pixel& source, unsigned mode) {
  if (mode > 10) throw std::runtime_error("unsupported Merge mode");
  if (mode == 4) return {std::uint16_t(base[0] & 32767), std::uint16_t(base[1] & 32767),
                         std::uint16_t(base[2] & 32767), std::uint16_t(gray(source))};
  Pixel result{};
  for (unsigned i = 0; i < 4; ++i) {
    const auto a = base[i], b = source[i];
    switch (mode) {
    case 0: result[i] = saturate(signed_lane(a) + signed_lane(b)); break;
    case 1: result[i] = std::max(0, int(a) - int(b)); break;
    case 2: result[i] = multiply(a, b); break;
    case 3: result[i] = std::uint16_t(a - b + 32767) >> 1; break;
    case 5: {
      const auto mask = signed_lane(b) > 16383 ? 32767 : 0;
      result[i] = multiply(a ^ mask, b ^ mask, 2) ^ mask;
      break;
    }
    case 6: {
      const auto doubled = std::uint16_t(b * 2);
      const auto mask = signed_lane(doubled) < 0 ? 65535 : 0;
      const auto weight = std::uint16_t(doubled & 32767);
      result[i] = std::uint16_t((multiply(a, weight) ^ mask) + (std::uint16_t(weight + a) & mask));
      break;
    }
    case 7: {
      const auto diff = saturate(signed_lane(b) - signed_lane(a));
      const auto mixed = saturate(signed_lane(a) + signed_lane(multiply(diff, source[3])));
      result[i] = std::max(0, signed_lane(mixed));
      break;
    }
    case 8: result[i] = multiply(a ^ 32767, b ^ 32767) ^ 32767; break;
    case 9: result[i] = std::uint16_t(std::min(signed_lane(a), signed_lane(b))); break;
    case 10: result[i] = std::uint16_t(std::max(signed_lane(a), signed_lane(b))); break;
    default: break;
    }
  }
  return result;
}

Pixel color_pixel(const Pixel& base, const Pixel& color, unsigned mode) {
  if (mode > 5) throw std::runtime_error("unsupported Color mode");
  if (mode == 3) {
    const auto value = std::uint16_t(gray(base));
    return {value, value, value, 32767};
  }
  if (mode < 3) return merge_pixel(base, color, mode == 0 ? 2 : mode - 1);
  Pixel result{};
  for (unsigned i = 0; i < 4; ++i) {
    if (mode == 4) result[i] = base[i] ^ 32767;
    else {
      const auto product = std::uint32_t(signed_lane(base[i]) * signed_lane(color[i]));
      result[i] = saturate(int(product >> 11));
    }
  }
  return result;
}

Pixel mask_pixel(const Pixel& mask,const Pixel& a,const Pixel& b,unsigned mode) {
  if(mode>3)throw std::runtime_error("invalid Mask mode");
  // Bitmap_Mask first converts the mask to gray, including alpha=0x7fff.
  const auto value=color_pixel(mask,{},3);
  if(mode==1)return merge_pixel(merge_pixel(value,a,2),b,0);
  if(mode==2)return merge_pixel(b,merge_pixel(value,a,2),1);
  const auto source=mode==3 ? merge_pixel(a,b,2) : a;Pixel out;
  for(unsigned c=0;c<4;++c) {
    const auto inverse=saturate(32767-signed_lane(value[c]));
    const auto first=(signed_lane(value[c])*signed_lane(source[c]))>>16;
    const auto second=(signed_lane(inverse)*signed_lane(b[c]))>>16;
    out[c]=std::uint16_t(signed_lane(saturate(first+second))*2);
  }
  return out;
}

Pixel range_pixel(const Pixel& source,const Pixel& low,const Pixel& high,unsigned mode) {
  if(mode>=6)throw std::runtime_error("invalid Range mode");
  auto value=source;if(mode&1)value=color_pixel(value,{},3);if(mode&2)value=color_pixel(value,{},4);
  Pixel out;
  for(unsigned c=0;c<4;++c) {
    const auto difference=saturate(signed_lane(high[c])-signed_lane(low[c]));
    const auto scaled=multiply(value[c],difference);
    out[c]=std::max(0,signed_lane(saturate(signed_lane(scaled)+signed_lane(low[c]))));
  }
  return out;
}

void draw_dots(Texture& texture, std::uint32_t color0, std::uint32_t color1,
               std::uint32_t density, std::uint32_t seed) {
  if (texture.pixels.empty() || density > 4096) throw std::runtime_error("invalid Dots density/texture");
  const auto a = color64(color0), b = color64(color1);
  const auto count = std::max<std::size_t>(1, texture.pixels.size() * density / 4096);
  Random random(seed);
  // The do/while in the reference draws one dot even for a zero density.
  for (std::size_t i = 0; i < count; ++i) {
    const auto fade = random.next() % 65536;
    const auto pixel = random.next() % texture.pixels.size();
    texture.pixels[pixel] = fade64(a, b, fade);
  }
}
} // namespace wz3
