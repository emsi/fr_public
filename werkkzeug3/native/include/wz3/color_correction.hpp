// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace wz3 {
struct ColorCorrectionEffect {std::array<std::uint32_t,11> parameters{};};
using ColorCorrectionConstants=std::array<std::array<float,4>,7>;
ColorCorrectionConstants color_correction_constants(std::span<const std::uint32_t>);
}
