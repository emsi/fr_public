// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <random>
#include <stdexcept>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

void test_composite() {
#if defined(__SSE2__)
  // Translate the reference MMX instructions directly to SSE2. Keep its BGRA
  // lane order here, independently of the portable RGBA implementation.
  const auto mask = _mm_set1_epi16(32767), zero = _mm_setzero_si128();
  const auto rgb = _mm_setr_epi16(32767, 32767, 32767, 0, 0, 0, 0, 0);
  const auto alpha = _mm_setr_epi16(0, 0, 0, 32767, 0, 0, 0, 0);
  std::minstd_rand random(1);
  for (unsigned trial = 0; trial < 4096; ++trial) {
    wz3::Pixel base{}, source{};
    for (auto& lane : base) lane = std::uint16_t(random());
    for (auto& lane : source) lane = std::uint16_t(random());
    const auto a = _mm_setr_epi16(base[2], base[1], base[0], base[3], 0, 0, 0, 0);
    const auto b = _mm_setr_epi16(source[2], source[1], source[0], source[3], 0, 0, 0, 0);
    for (unsigned mode = 0; mode <= 10; ++mode) {
      __m128i value = zero;
      switch (mode) {
      case 0: value = _mm_adds_epi16(a, b); break;
      case 1: value = _mm_subs_epu16(a, b); break;
      case 2: value = _mm_slli_epi16(_mm_mulhi_epi16(a, b), 1); break;
      case 3: value = _mm_srli_epi16(_mm_add_epi16(_mm_sub_epi16(a, b), mask), 1); break;
      case 4: {
        const auto g = _mm_srli_epi64(b, 16), r = _mm_srli_epi64(b, 32);
        const auto gray = _mm_add_epi16(_mm_srli_epi16(_mm_add_epi16(b, g), 1),
                                        _mm_srli_epi16(_mm_add_epi16(r, g), 1));
        value = _mm_or_si128(_mm_and_si128(a, rgb), _mm_and_si128(_mm_slli_epi64(gray, 47), alpha));
        break;
      }
      case 5: {
        const auto invert = _mm_srli_epi16(_mm_cmpgt_epi16(b, _mm_set1_epi16(16383)), 1);
        value = _mm_xor_si128(_mm_slli_epi16(_mm_mulhi_epi16(_mm_xor_si128(a, invert),
                              _mm_xor_si128(b, invert)), 2), invert);
        break;
      }
      case 6: {
        const auto doubled = _mm_slli_epi16(b, 1);
        const auto sign = _mm_srai_epi16(doubled, 15), weight = _mm_and_si128(doubled, mask);
        value = _mm_add_epi16(_mm_xor_si128(_mm_slli_epi16(_mm_mulhi_epi16(a, weight), 1), sign),
                              _mm_and_si128(_mm_add_epi16(weight, a), sign));
        break;
      }
      case 7: {
        const auto opacity = _mm_shufflelo_epi16(b, _MM_SHUFFLE(3, 3, 3, 3));
        value = _mm_max_epi16(zero, _mm_adds_epi16(a,
          _mm_slli_epi16(_mm_mulhi_epi16(_mm_subs_epi16(b, a), opacity), 1)));
        break;
      }
      case 8: value = _mm_xor_si128(_mm_slli_epi16(_mm_mulhi_epi16(_mm_xor_si128(a, mask),
                                                _mm_xor_si128(b, mask)), 1), mask); break;
      case 9: value = _mm_min_epi16(a, b); break;
      case 10: value = _mm_max_epi16(a, b); break;
      }
      wz3::Pixel bgra{};
      _mm_storel_epi64(reinterpret_cast<__m128i*>(bgra.data()), value);
      const wz3::Pixel expected{bgra[2], bgra[1], bgra[0], bgra[3]};
      if (wz3::merge_pixel(base, source, mode) != expected)
        throw std::runtime_error("Merge differs from SIMD oracle, mode " + std::to_string(mode));
    }
  }
#endif
  wz3::Texture dots;
  dots.width = dots.height = 8;
  dots.pixels.assign(64, wz3::color64(0xff000000));
  wz3::draw_dots(dots, 0xff000000, 0xffffffff, 512, 0);
  // Golden positions/brightness from the original sGetRnd/sSetRndSeed stream.
  const std::pair<unsigned, unsigned> expected[] = {
    {63, 29326}, {16, 9002}, {12, 21930}, {53, 9576}, {6, 17102}, {4, 4972}, {42, 10554}
  };
  for (const auto [index, value] : expected)
    if (dots.pixels[index][0] != value) throw std::runtime_error("Dots random sequence changed");
  unsigned count = 0;
  for (const auto& pixel : dots.pixels) count += pixel[0] != 0;
  if (count != 7) throw std::runtime_error("Dots overwrite semantics changed");
}
