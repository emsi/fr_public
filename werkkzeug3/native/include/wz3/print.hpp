// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
#include "wz3/texture.hpp"
namespace wz3 {
struct PrintLayout {
  std::vector<SceneEffectVertex> vertices; // Perimeter-ordered glyph quads.
  bool normalized{};
};
// EffectPrintInfo and the literal-text/alignment path of Exec_Effect_Print.
// Game-state/FPS substitutions need a runtime provider and are rejected.
PrintLayout layout_print(std::span<const std::uint32_t>,std::string_view,
                         const std::array<GlyphMetric,256>&,bool legacy_alignment=false);
SceneMatrix print_normalized_projection(unsigned width,unsigned height);
} // namespace wz3
