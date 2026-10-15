// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/texture.hpp"
#include <functional>
namespace wz3::detail {
struct TextLine {int x,y;std::string_view text;};
// Coordinates and metrics are in the original 4x supersampled bitmap space.
std::vector<TextLine> paragraph_lines(const FontLayout&,unsigned width,unsigned height,int line_height,
                                    const std::function<int(std::string_view)>& measure);
void composite_text_mask(Texture&,std::span<const std::uint8_t>,std::uint32_t color);
}
