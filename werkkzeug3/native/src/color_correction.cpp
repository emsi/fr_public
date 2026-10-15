// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/color_correction.hpp"
#include <bit>
#include <cfenv>
#include <cmath>
#include <stdexcept>
namespace wz3 {
ColorCorrectionConstants color_correction_constants(std::span<const std::uint32_t> p) {
  if(p.size()!=11)throw std::runtime_error("invalid Color Correction parameter count");
  struct Nearest {std::fenv_t previous;Nearest(){std::fegetenv(&previous);std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetenv(&previous);}} rounding;
  const auto real=[&](unsigned i){const auto v=std::bit_cast<float>(p[i]);if(!std::isfinite(v) || std::abs(v)>1e6f)throw std::runtime_error("invalid Color Correction numeric parameter");return v;};
  const auto color=[](std::uint32_t c){return std::array<float,4>{((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f,(c>>24)/255.f};};
  const auto t0=real(1),t1=real(2),amp=real(10);const auto interval=t1-t0;
  if(!interval || !std::isfinite(1.f/interval))throw std::runtime_error("Color Correction has undefined threshold interval");
  ColorCorrectionConstants c{};c[4]={-t0/interval,1.f/interval,0,0};
  if(p[7]&1){c[2]={1,1,1,1};c[4][2]=1;}
  else {c[0]=color(p[3]);c[1]=color(p[4]);c[2]=color(p[5]);for(auto& v:c[2])v*=amp;c[3]=color(p[6]);c[5]=color(p[8]);c[6]=color(p[9]);}
  return c;
}
}
