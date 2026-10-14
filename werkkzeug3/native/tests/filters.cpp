// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

extern void check(bool, const char*);
namespace {
using wz3::Texture;
using wz3::Pixel;
int lane(std::uint16_t v) { return std::bit_cast<std::int16_t>(v); }
#if defined(__SSE2__)
Pixel bilinear_oracle(const Pixel& a, const Pixel& b, const Pixel& c, const Pixel& d, unsigned u, unsigned v) {
  auto top = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(a.data()));
  auto right = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(b.data()));
  auto bottom = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(c.data()));
  auto bottom_right = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(d.data()));
  const auto x = _mm_set1_epi16((u & 65535) >> 1), y = _mm_set1_epi16((v & 65535) >> 1);
  top = _mm_add_epi16(top, _mm_slli_epi16(_mm_mulhi_epi16(_mm_sub_epi16(right, top), x), 1));
  bottom = _mm_add_epi16(bottom, _mm_slli_epi16(_mm_mulhi_epi16(_mm_sub_epi16(bottom_right, bottom), x), 1));
  const auto delta = _mm_slli_epi16(_mm_mulhi_epi16(_mm_sub_epi16(bottom, top), y), 1);
  const auto adjust = _mm_set1_epi16(-32768);
  const auto out = _mm_subs_epu16(_mm_add_epi16(_mm_add_epi16(top, adjust), delta), adjust);
  Pixel result{};
  _mm_storel_epi64(reinterpret_cast<__m128i*>(result.data()), out);
  return result;
}
#endif
std::uint16_t blur_scale_oracle(int value, int amp, int clip) {
#if defined(__SSE2__)
  const auto sum = _mm_set1_epi32(value);
  const auto shifted = _mm_srai_epi32(sum, 6);
  const auto low = _mm_set1_epi16(_mm_extract_epi16(shifted, 0));
  const auto high = _mm_set1_epi16(_mm_extract_epi16(shifted, 1));
  const auto amp_low = _mm_set1_epi16(amp), amp_high = _mm_set1_epi16(amp >> 16);
  auto overflow = _mm_cmpgt_epi32(sum, _mm_set1_epi32(clip));
  auto out = _mm_packs_epi32(overflow, overflow);
  out = _mm_adds_epu16(out, _mm_mullo_epi16(high, amp_low));
  out = _mm_adds_epu16(out, _mm_mullo_epi16(low, amp_high));
  out = _mm_adds_epu16(out, _mm_mulhi_epu16(low, amp_low));
  const auto adjust = _mm_set1_epi16(-32768);
  out = _mm_subs_epi16(_mm_add_epi16(out, adjust), adjust);
  return _mm_extract_epi16(out, 0);
#else
  const auto s = std::uint32_t(value >> 6), a = std::uint32_t(amp);
  const auto sum = (value > clip ? 65535u : 0u) + (((s >> 16) * (a & 65535)) & 65535) +
                   (((s & 65535) * (a >> 16)) & 65535) + (((s & 65535) * (a & 65535)) >> 16);
  return std::min(sum, 32767u);
#endif
}
// Independent direct convolution, not the rolling accumulator of BlurCore.
void blur_reference(Texture& texture, unsigned flags, float sx, float sy, float amplitude) {
  const float radii[] = {sx, sy};
  for (unsigned axis = 0; axis < 2; ++axis) {
    const auto length = axis ? texture.height : texture.width;
    const auto size = int(std::nearbyint(128 * radii[axis] * length));
    const int kernel = 2 * (size / 128) + ((flags & 16) ? 1 : 0), edge = (size & 127) / 2;
    const int amp = int(amplitude * 65536.0f * 64) / std::max(1, kernel * 64 + edge * 2);
    const int clip = amp > 128 ? int((std::int64_t(65536 * 64) * 65536) / amp - 1) : INT32_MAX;
    for (unsigned pass = 0; pass < (flags & 15); ++pass) {
      const auto input = texture;
      for (unsigned y = 0; y < texture.height; ++y) for (unsigned x = 0; x < texture.width; ++x) {
        for (unsigned c = 0; c < 4; ++c) {
          std::uint32_t sum = 0;
          for (int k = 0; k <= kernel + 1; ++k) {
            const auto offset = std::uint32_t(k - ((kernel + 1) / 2));
            const auto px = axis ? x : (x + offset) & (texture.width - 1);
            const auto py = axis ? (y + offset) & (texture.height - 1) : y;
            sum += std::uint32_t(lane(input.pixels[py * texture.width + px][c]) *
                                ((k == 0 || k == kernel + 1) ? edge : 64));
          }
          texture.pixels[y * texture.width + x][c] = blur_scale_oracle(std::bit_cast<std::int32_t>(sum), amp, clip);
        }
      }
    }
  }
}
}

void test_filters() {
  Texture source; source.width = 8; source.height = 4; source.pixels.resize(32);
  std::uint32_t state = 19;
  for (auto& p : source.pixels) for (auto& c : p) { state = state * 1664525 + 1013904223; c = state >> 16; }
#if defined(__SSE2__)
  for (unsigned flags = 0; flags < 4; ++flags) for (unsigned i = 0; i < 4096; ++i) {
    state = state * 1664525 + 1013904223;
    const auto u = int(state % (16 * 65536)) - 4 * 65536;
    state = state * 1664525 + 1013904223;
    const auto v = int(state % (8 * 65536)) - 2 * 65536;
    auto x = flags & 1 ? std::clamp(u, 0, 8 * 65536 - 1) : u & (8 * 65536 - 1);
    auto y = flags & 2 ? std::clamp(v, 0, 4 * 65536 - 1) : v & (4 * 65536 - 1);
    const auto x0 = x >> 16, y0 = y >> 16, x1 = (x0 + 1) % 8, y1 = (y0 + 1) % 4;
    const auto expected = bilinear_oracle(source.pixels[y0*8+x0], source.pixels[y0*8+x1],
                                          source.pixels[y1*8+x0], source.pixels[y1*8+x1], x, y);
    check(wz3::sample_bitmap(source, u, v, flags) == expected, "bilinear SIMD mismatch");
  }
#endif
  check(wz3::sample_bitmap(source, -65536, -65536, 7) == source.pixels.back(), "point sampler always wraps");
  for (unsigned flags : {1u, 2u, 17u, 18u, 19u}) for (float radius : {0.0f, 0.1f, 0.25f, 1.0f})
    for (float amplitude : {0.0f, 0.7f, 1.0f, 3.0f}) {
      auto actual = source, expected = source, scratch = source;
      wz3::blur_bitmap(actual, scratch, flags, radius, radius * 0.75f, amplitude);
      blur_reference(expected, flags, radius, radius * 0.75f, amplitude);
      check(actual.pixels == expected.pixels, "blur differs from convolution/SIMD reference");
    }
  // Identity/translation check matrix construction as well as pixel sampling.
  for (auto& p : source.pixels) for (auto& c : p) c &= 32767;
  auto dest = source;
  wz3::transform_bitmap(source, dest, 0, 1, 1, 0.5f, 0.5f, 0);
  check(dest.pixels == source.pixels, "identity transform");
  wz3::transform_bitmap(source, dest, 0, 1, 1, 0.625f, 0.5f, 0);
  for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 8; ++x)
    check(dest.pixels[y*8+x] == source.pixels[y*8+(x+1)%8], "one-pixel transform translation");
  dest = source;
  wz3::adjust_hscb(dest, 0, 1, 1, 1);
  check(dest.pixels == source.pixels, "HSCB identity");
  wz3::adjust_hscb(dest, 0, 0, 1, 1);
  for (const auto& p : dest.pixels) check(p[0] == p[1] && p[1] == p[2], "HSCB desaturation");
  for (auto& p : source.pixels) p[2] = 10000;
  wz3::normal_bitmap(source, dest, 1, 1);
  for (const auto& p : dest.pixels) check(p == Pixel{32767, 16384, 16384, 65535}, "normals must derive from blue only");
  bool rejected = false;
  try { wz3::transform_bitmap(source, dest, 0, 0, 1, 0.5f, 0.5f, 0); }
  catch (const std::runtime_error&) { rejected = true; }
  check(rejected, "zero transform scale must fail");
}
