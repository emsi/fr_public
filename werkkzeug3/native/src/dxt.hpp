// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace wz3::legacy {
// Original A8R8G8B8 words in a 4x4 block; output is 8 (DXT1) or 16 (DXT5) bytes.
void dxt_block(std::span<const std::uint32_t,16>,std::span<std::uint8_t>,bool alpha,unsigned quality);
}
