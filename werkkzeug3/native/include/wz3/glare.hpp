// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace wz3 {
struct GlareEffect {std::array<std::uint32_t,11> parameters{};};
struct GlarePass {
  enum Mode { filter,copy,tone,combine } mode=filter;
  // 0: captured viewport; 1: 512x256; 2: 256x128; 3: viewport output.
  unsigned source{},target{},width{},height{};
  std::array<float,2> uv{1,1},original_uv{1,1};
  std::array<std::array<float,3>,9> taps{}; // U/V offset and weight
  std::array<std::array<float,4>,3> constants{};
};
// Capture storage follows RenderTargetManager: grow, never shrink. Passes keep
// the original RGBA8 write between each filter, tone and combine operation.
std::vector<GlarePass> glare_passes(std::span<const std::uint32_t>,unsigned width,unsigned height,
                                  unsigned capture_width,unsigned capture_height);
}
