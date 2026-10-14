// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <iostream>
#include <stdexcept>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
  try {
    check(wz3::color64(0xff804020) == wz3::Pixel{16448, 8224, 4112, 32767}, "ARGB conversion");
    const auto black = wz3::color64(0xff000000), white = wz3::color64(0xffffffff);
    check(wz3::fade64(black, white, 32768) == wz3::Pixel{16382, 16382, 16382, 32764}, "MMX midpoint rounding");
#if defined(__SSE2__)
    // Independent machine-instruction oracle for the original MMX sequence.
    // SSE2 gives the same signed lane arithmetic without MMX/FPU state changes.
    const wz3::Pixel a{0, 1, 16383, 32767}, b{32767, 22000, 64, 0};
    for (int fade = 0; fade <= 65535; ++fade) {
      auto weights = _mm_set1_epi32(fade);
      weights = _mm_srai_epi32(weights, 1);
      weights = _mm_xor_si128(weights, _mm_set_epi32(-1, 0, -1, 0));
      weights = _mm_add_epi32(weights, _mm_set_epi32(32769, 0, 32769, 0));
      const auto packed = _mm_packs_epi32(weights, weights);
      const int w1 = _mm_extract_epi16(packed, 0), w0 = _mm_extract_epi16(packed, 1);
      const auto va = _mm_setr_epi16(a[0], a[1], a[2], a[3], 0, 0, 0, 0);
      const auto vb = _mm_setr_epi16(b[0], b[1], b[2], b[3], 0, 0, 0, 0);
      const auto sum = _mm_slli_epi16(_mm_add_epi16(_mm_mulhi_epi16(va, _mm_set1_epi16(w0)),
                                                  _mm_mulhi_epi16(vb, _mm_set1_epi16(w1))), 1);
      wz3::Pixel expected{};
      _mm_storel_epi64(reinterpret_cast<__m128i*>(expected.data()), sum);
      check(wz3::fade64(a, b, fade) == expected, "scalar interpolation differs from SIMD oracle");
    }
#endif
    wz3::Project project;
    project.pages.resize(1);
    wz3::Operator op;
    op.command = 0x21; op.width = 1; op.name = "flat";
    op.parameters = {2, 1, 0xff804020};
    project.operators.push_back(op);
    auto texture = wz3::evaluate_texture(project, wz3::connect(project), 0);
    check(texture->width == 4 && texture->height == 2 && texture->pixels.size() == 8, "dimension exponents");
    check(texture->pixels.front() == wz3::color64(0xff804020), "flat color");
    project.operators[0].command = 0x22;
    bool rejected = false;
    try { wz3::evaluate_texture(project, wz3::connect(project), 0); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "unimplemented operator must fail");
    std::cout << "texture arithmetic and explicit unsupported-operator failure passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
