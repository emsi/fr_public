// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Portable arithmetic from BilinearFilter, Bitmap_Rotate, BlurCore,
// Bitmap_Blur, Bitmap_Normals and Bitmap_HSCB in ../genbitmap.cpp.
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace wz3 {
namespace {
int lane(std::uint16_t v) { return std::bit_cast<std::int16_t>(v); }
std::int32_t wrap(std::uint32_t v) { return std::bit_cast<std::int32_t>(v); }
int checked_int(double v) {
  if (!std::isfinite(v) || v < -2147483648.0 || v > 2147483647.0)
    throw std::runtime_error("filter arithmetic exceeds int32 range");
  return static_cast<int>(v);
}
int rounded(float v) { return checked_int(std::nearbyint(double(v))); }
void validate(const Texture& t) {
  if (!std::has_single_bit(t.width) || !std::has_single_bit(t.height) ||
      t.width > 4096 || t.height > 4096 || t.pixels.size() != std::size_t(t.width) * t.height)
    throw std::runtime_error("invalid filter texture dimensions");
}
void same_size(const Texture& a, const Texture& b) {
  validate(a); validate(b);
  if (&a == &b || a.width != b.width || a.height != b.height)
    throw std::runtime_error("filter needs separate, equally sized textures");
}
std::uint16_t interpolate(std::uint16_t a, std::uint16_t b, int fraction) {
  return std::uint16_t(a + ((lane(std::uint16_t(b - a)) * fraction) >> 16) * 2);
}
Pixel sample(const Texture& t, std::int32_t u, std::int32_t v, unsigned border) {
  // PointFilter historically wraps even when the clamp flags are selected.
  if (border & 4) return t.pixels[((std::uint32_t(v) >> 16) & (t.height - 1)) * t.width +
                                ((std::uint32_t(u) >> 16) & (t.width - 1))];
  const auto xmax = (t.width << 16) - 1, ymax = (t.height << 16) - 1;
  const auto x = border & 1 ? std::uint32_t(std::clamp(u, 0, int(xmax))) : std::uint32_t(u) & xmax;
  const auto y = border & 2 ? std::uint32_t(std::clamp(v, 0, int(ymax))) : std::uint32_t(v) & ymax;
  const auto x0 = x >> 16, y0 = y >> 16, x1 = (x0 + 1) & (t.width - 1), y1 = (y0 + 1) & (t.height - 1);
  const auto& a = t.pixels[y0 * t.width + x0]; const auto& b = t.pixels[y0 * t.width + x1];
  const auto& c = t.pixels[y1 * t.width + x0]; const auto& d = t.pixels[y1 * t.width + x1];
  Pixel out{};
  for (unsigned k = 0; k < 4; ++k) {
    const auto top = interpolate(a[k], b[k], (x & 65535) >> 1);
    const auto bottom = interpolate(c[k], d[k], (x & 65535) >> 1);
    const auto value = std::uint16_t(interpolate(top, bottom, (y & 65535) >> 1) + 32768);
    out[k] = std::max(0, int(value) - 32768); // paddw adjust, then psubusw
  }
  return out;
}
std::uint16_t blur_scale(std::int32_t sum, std::uint32_t amp, std::int32_t clip) {
  const auto shifted = std::uint32_t(sum >> 6);
  const auto low = shifted & 65535, high = shifted >> 16;
  const auto amp_low = amp & 65535, amp_high = amp >> 16;
  unsigned out = sum > clip ? 65535 : 0;
  out = std::min(65535u, out + ((high * amp_low) & 65535));
  out = std::min(65535u, out + ((low * amp_high) & 65535));
  out = std::min(65535u, out + ((low * amp_low) >> 16));
  return std::min(out, 32767u);
}
void blur_line(const Texture& source, Texture& dest, std::size_t start, unsigned stride,
               unsigned length, unsigned kernel, int f1, std::uint32_t amp, std::int32_t clip) {
  const auto mask = length - 1;
  unsigned first = (0u - ((kernel + 1) >> 1)) & mask, last = first;
  const int f0 = 64 - f1;
  std::array<std::uint32_t, 4> sums{};
  const auto& initial = source.pixels[start + first * stride];
  for (unsigned c = 0; c < 4; ++c) sums[c] = std::uint32_t(lane(initial[c]) * (f1 - f0));
  const auto accumulate = [&](unsigned index, bool remove) {
    const auto& a = source.pixels[start + index * stride];
    const auto& b = source.pixels[start + ((index + 1) & mask) * stride];
    for (unsigned c = 0; c < 4; ++c) {
      const auto value = std::uint32_t(lane(a[c]) * (remove ? f1 : f0) + lane(b[c]) * (remove ? f0 : f1));
      if (remove) sums[c] -= value; else sums[c] += value;
    }
  };
  for (unsigned k = 0; k < kernel; ++k) { accumulate(last, false); last = (last + 1) & mask; }
  for (unsigned x = 0; x < length; ++x) {
    accumulate(last, false); last = (last + 1) & mask;
    auto& p = dest.pixels[start + x * stride];
    for (unsigned c = 0; c < 4; ++c) p[c] = blur_scale(wrap(sums[c]), amp, clip);
    accumulate(first, true); first = (first + 1) & mask;
  }
}
} // namespace

Pixel sample_bitmap(const Texture& texture, std::int32_t u, std::int32_t v, unsigned border) {
  validate(texture);
  if (border > 7) throw std::runtime_error("invalid transform border flags");
  return sample(texture, u, v, border);
}

void transform_bitmap(const Texture& source, Texture& dest, float angle, float sx, float sy,
                      float tx, float ty, unsigned border) {
  validate(source); validate(dest);
  if (&source == &dest || sx == 0 || sy == 0 || border > 7)
    throw std::runtime_error("invalid transform parameters");
  const float rotation = angle * (2.0f * std::numbers::pi_v<float>);
  const auto cs = std::cos(double(rotation)), sn = std::sin(double(rotation));
  const auto m00 = rounded(cs * 65536 * source.width / (sx * dest.width));
  const auto m01 = rounded(sn * 65536 * source.height / (sx * dest.width));
  const auto m10 = rounded(-sn * 65536 * source.width / (sy * dest.height));
  const auto m11 = rounded(cs * 65536 * source.height / (sy * dest.height));
  const auto m20 = rounded(tx * source.width * 65536 - wrap(dest.width * std::uint32_t(m00) + dest.height * std::uint32_t(m10)) / 2);
  const auto m21 = rounded(ty * source.height * 65536 - wrap(dest.width * std::uint32_t(m01) + dest.height * std::uint32_t(m11)) / 2);
  for (unsigned y = 0; y < dest.height; ++y) {
    auto u = y * std::uint32_t(m10) + std::uint32_t(m20);
    auto v = y * std::uint32_t(m11) + std::uint32_t(m21);
    for (unsigned x = 0; x < dest.width; ++x) {
      dest.pixels[std::size_t(y) * dest.width + x] = sample(source, wrap(u), wrap(v), border);
      u += std::uint32_t(m00); v += std::uint32_t(m01);
    }
  }
}

void blur_bitmap(Texture& texture, Texture& scratch, unsigned flags, float sx, float sy, float amplitude) {
  same_size(texture, scratch);
  if (flags > 31 || sx < 0 || sy < 0 || amplitude < 0)
    throw std::runtime_error("invalid blur parameters");
  const auto order = flags & 15;
  if (!order) return;
  const int sizes[] = {rounded(128 * sx * texture.width), rounded(128 * sy * texture.height)};
  const auto famp = checked_int(amplitude * 65536.0f * 64);
  for (unsigned axis = 0; axis < 2; ++axis) {
    if (sizes[axis] < 0 || sizes[axis] > 128 * 16384)
      throw std::runtime_error("blur kernel exceeds resource limit");
    const auto kernel = (sizes[axis] / 128) * 2 + ((flags & 16) ? 1 : 0);
    const auto f1 = (sizes[axis] & 127) / 2;
    const auto amp = famp / std::max(1, kernel * 64 + f1 * 2);
    const auto clip = amp > 128 ? std::int32_t((std::int64_t(65536 * 64) * 65536) / amp - 1) : INT32_MAX;
    for (unsigned pass = 0; pass < order; ++pass) {
      const auto length = axis ? texture.height : texture.width;
      const auto lines = axis ? texture.width : texture.height;
      const auto stride = axis ? texture.width : 1;
      for (unsigned line = 0; line < lines; ++line)
        blur_line(texture, scratch, axis ? line : std::size_t(line) * length, stride, length, kernel, f1, amp, clip);
      texture.pixels.swap(scratch.pixels);
    }
  }
}

void normal_bitmap(const Texture& source, Texture& dest, float distance, unsigned mode) {
  same_size(source, dest);
  if (mode > 7) throw std::runtime_error("invalid Normals mode");
  const auto dist = rounded(distance * 65536.0f);
  const auto shiftx = 20 - std::countr_zero(source.width), shifty = 20 - std::countr_zero(source.height);
  const auto at = [&](unsigned x, unsigned y) { return int(source.pixels[(y & (source.height - 1)) * source.width + (x & (source.width - 1))][2]); };
  for (unsigned y = 0; y < source.height; ++y) for (unsigned x = 0; x < source.width; ++x) {
    int vx, vy;
    if (mode & 4) { vx = 4 * (at(x-1, y) - at(x, y)); vy = 4 * (at(x, y-1) - at(x, y)); }
    else {
      vx = at(x-2,y) + 3*at(x-1,y) - 3*at(x,y) - at(x+1,y);
      vy = at(x,y-2) + 3*at(x,y-1) - 3*at(x,y) - at(x,y+1);
    }
    vx = std::clamp((wrap(std::uint32_t(vx) * std::uint32_t(dist >> 4)) >> shiftx) + 16384, 0, 32767) - 16384;
    vy = std::clamp((wrap(std::uint32_t(vy) * std::uint32_t(dist >> 4)) >> shifty) + 16384, 0, 32767) - 16384;
    int vz = 0;
    if (mode & 1) {
      vz = 16383 * 16383 - vx * vx - vy * vy;
      if (vz > 0) vz = int(std::sqrt(double(vz)));
      else {
        const float factor = 16383 / std::sqrt(double(vx * vx + vy * vy));
        vx = int(vx * factor); vy = int(vy * factor); vz = 0;
      }
    }
    if (mode & 2) { std::swap(vx, vy); vy = -vy; }
    dest.pixels[std::size_t(y) * source.width + x] = {
      std::uint16_t(vz + 16384), std::uint16_t(vy + 16384), std::uint16_t(vx + 16384), 65535};
  }
}

void adjust_hscb(Texture& texture, float hue, float saturation, float contrast, float brightness) {
  validate(texture);
  const float gamma = contrast * contrast;
  std::array<std::int64_t, 1025> table{};
  for (unsigned i = 0; i < table.size(); ++i)
    table[i] = checked_int(std::pow((i * 32 + 0.01) / 32768.0f, double(gamma)) * 32768.0f * brightness);
  const auto hue_shift = checked_int(hue * 6 * 65536) % (6 * 65536);
  const auto sat = checked_int(saturation * 65536);
  const auto lookup = [&](unsigned channel) {
    if (channel > 32767) throw std::runtime_error("HSCB requires 15-bit RGB channels");
    const auto index = channel >> 5;
    return table[index] + (((table[index+1] - table[index]) * (channel & 31)) >> 5);
  };
  for (auto& p : texture.pixels) {
    auto r = lookup(p[0]), g = lookup(p[1]), b = lookup(p[2]);
    if (hue_shift != 0 || sat != 65536) {
      const auto high = std::max({r,g,b});
      auto low = std::min({r,g,b}), range = high - low;
      std::int64_t h = 0;
      if (range) {
        if (r == high) h = 65536 + ((g-b)*65536)/range;
        else if (g == high) h = 3*65536 + ((b-r)*65536)/range;
        else h = 5*65536 + ((r-g)*65536)/range;
      }
      h += hue_shift; range = (range * sat) >> 16; low = high - range;
      if (h > 6*65536) h -= 6*65536;
      const auto rem = h & 131071;
      const auto m1 = low + (rem >= 65536 ? ((rem - 65536) * range) >> 16 : 0);
      const auto m2 = low + (rem < 65536 ? ((65536 - rem) * range) >> 16 : 0);
      if (h < 2*65536) { r = high; g = m1; b = m2; }
      else if (h < 4*65536) { r = m2; g = high; b = m1; }
      else { r = m1; g = m2; b = high; }
    }
    p[0] = std::clamp<std::int64_t>(r, 0, 32767);
    p[1] = std::clamp<std::int64_t>(g, 0, 32767);
    p[2] = std::clamp<std::int64_t>(b, 0, 32767);
  }
}
} // namespace wz3
