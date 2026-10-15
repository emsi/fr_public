// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include "dxt.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace wz3::legacy::dxt {
using sInt=std::int32_t;using sU8=std::uint8_t;using sU16=std::uint16_t;using sU32=std::uint32_t;using sF32=float;using sBool=bool;
constexpr bool sFALSE=false;
template<class T>T sMin(T a,T b){return a<b ? a : b;}template<class T>T sMax(T a,T b){return a>b ? a : b;}
template<class T>T sRange(T a,T high,T low){return std::clamp(a,low,high);}
template<class T>void sSwap(T& a,T& b){std::swap(a,b);}
int sAbs(int v){return std::abs(v);}double sFAbs(double v){return std::abs(v);}
void sSetMem(void* d,int v,std::size_t n){std::memset(d,v,n);}
#define sLINK_RYGDXT 1
#include "dxt_kernel.inc"
#undef sLINK_RYGDXT
}
namespace wz3 {
namespace {
struct Nearest {std::fenv_t previous;Nearest(){std::fegetenv(&previous);std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetenv(&previous);}};
void init(){static const bool ready=[](){legacy::dxt::sInitDXT();return true;}();static_cast<void>(ready);}
}
void legacy::dxt_block(std::span<const std::uint32_t,16> input,std::span<std::uint8_t> output,bool alpha,unsigned quality) {
  static_assert(std::endian::native==std::endian::little,"archived DXT codec needs little-endian words");
  if(output.size()!=(alpha ? 16u : 8u) || quality>0x3ffffff)throw std::runtime_error("invalid DXT block arguments");
  const Nearest rounding;init();std::array<std::uint32_t,4> aligned{};
  legacy::dxt::sCompressDXTBlock(reinterpret_cast<std::uint8_t*>(aligned.data()),input.data(),alpha,quality);
  std::memcpy(output.data(),aligned.data(),output.size());
}
std::vector<CompressedTextureLevel> compressed_texture_levels(const Texture& texture) {
  if((texture.format!=8 && texture.format!=9) || !std::has_single_bit(texture.width) || !std::has_single_bit(texture.height))
    throw std::runtime_error("DXT requires a DXT1/DXT5 power-of-two texture");
  const Nearest rounding;init();
  // Keep the existing 15-bit mip reduction and byte packing, then compress
  // each level. Tiny mip blocks wrap at their own size, as UpdateTexture did.
  auto color=texture;color.format=1;const auto colors=color_texture_levels(color);
  const bool alpha=texture.format==9;const unsigned block_bytes=alpha ? 16 : 8;
  std::vector<CompressedTextureLevel> result;
  for(const auto& level:colors) {
    CompressedTextureLevel output{level.width,level.height,{}};
    output.blocks.resize(std::size_t((level.width+3)/4)*((level.height+3)/4)*block_bytes);std::size_t at=0;
    for(unsigned y=0;y<level.height;y+=4)for(unsigned x=0;x<level.width;x+=4) {
      std::array<std::uint32_t,16> input;std::array<std::uint32_t,4> encoded{};
      for(unsigned dy=0;dy<4;++dy)for(unsigned dx=0;dx<4;++dx) {
        const auto i=(((y+dy)&(level.height-1))*level.width+((x+dx)&(level.width-1)))*4;
        input[dy*4+dx]=std::uint32_t(level.pixels[i+2])|(std::uint32_t(level.pixels[i+1])<<8)|
          (std::uint32_t(level.pixels[i])<<16)|(std::uint32_t(level.pixels[i+3])<<24);
      }
      legacy::dxt::sCompressDXTBlock(reinterpret_cast<std::uint8_t*>(encoded.data()),input.data(),alpha,texture.mip_threshold>>6);
      std::memcpy(output.blocks.data()+at,encoded.data(),block_bytes);at+=block_bytes;
    }
    result.push_back(std::move(output));
  }
  return result;
}
} // namespace wz3
