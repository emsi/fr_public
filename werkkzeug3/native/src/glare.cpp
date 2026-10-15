// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/glare.hpp"
#include <bit>
#include <cfenv>
#include <cmath>
#include <stdexcept>
namespace wz3 {
std::vector<GlarePass> glare_passes(std::span<const std::uint32_t> p,unsigned width,unsigned height,unsigned cw,unsigned ch) {
  if(p.size()!=11 || !width || !height || cw<width || ch<height || cw>4096 || ch>4096)
    throw std::runtime_error("invalid Glare parameters or capture size");
  struct Nearest {std::fenv_t previous;Nearest(){std::fegetenv(&previous);std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetenv(&previous);}} rounding;
  const auto real=[&](unsigned i){const auto v=std::bit_cast<float>(p[i]);if(!std::isfinite(v) || std::abs(v)>1e6f)throw std::runtime_error("invalid Glare numeric parameter");return v;};
  const auto color=[](std::uint32_t c){return std::array<float,4>{((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f,(c>>24)/255.f};};
  const std::array<unsigned,3> widths{cw,512,256},heights{ch,256,128};
  const std::array<float,2> original_uv{float(width)/cw,float(height)/ch};
  std::vector<GlarePass> result;unsigned source=0;
  const auto next=[&](GlarePass::Mode mode,unsigned target) {
    GlarePass pass;pass.mode=mode;pass.source=source;pass.target=target;
    pass.width=target==3 ? width : widths[target];pass.height=target==3 ? height : heights[target];
    if(source==0)pass.uv=original_uv;
    pass.original_uv=original_uv;return pass;
  };
  for(unsigned stage=0;stage<2;++stage) {
    const auto down=p[1]&3;
    if((stage==0 && down==1) || (stage==1 && down!=1 && down!=2))continue;
    auto pass=next(p[1]&4 ? GlarePass::copy : GlarePass::filter,stage+1);
    constexpr float offset[]={-1.2f,-0.f,1.2f},weight[]={5.f/16,6.f/16,5.f/16};
    for(unsigned i=0;i<9;++i)pass.taps[i]={offset[i%3]/widths[source],offset[i/3]/heights[source],weight[i%3]*weight[i/3]};
    // effect_glare.psh seeds its accumulator with c0, including c0.w.
    pass.constants[0][3]=pass.taps[0][2];result.push_back(pass);source=stage+1;
  }
  auto tone=next(GlarePass::tone,source);tone.constants[0]=color(p[2]);
  auto& threshold=tone.constants[0];const float divisor=(1-threshold[0])+(1-threshold[1])+(1-threshold[2]);
  if(divisor==0)throw std::runtime_error("Glare white threshold has undefined original normalization");
  threshold[3]=1.f/divisor;const float gray=real(9);tone.constants[1]={gray,1-gray,0,0};result.push_back(tone);
  constexpr float weights[]={1.f/25,2.f/25,3.f/25,4.f/25,5.f/25,4.f/25,3.f/25,2.f/25,1.f/25};
  for(unsigned stage=0;stage<2;++stage) {
    const float radius=real(5+stage),amp=real(7+stage);if(radius<=0)continue;
    for(unsigned vertical=0;vertical<2;++vertical) {
      auto pass=next(GlarePass::filter,source);
      for(unsigned i=0;i<9;++i) {
        const float offset=(int(i)-4)*radius;
        pass.taps[i]=vertical ? std::array<float,3>{0,offset*(float(widths[source])/heights[source]),weights[i]*amp} : std::array<float,3>{offset,0,weights[i]};
      }
      pass.constants[0][3]=pass.taps[0][2];result.push_back(pass);
    }
  }
  auto merge=next(GlarePass::combine,3);merge.constants[0]=color(p[3]);merge.constants[1]=color(p[4]);merge.constants[2][0]=real(10);result.push_back(merge);
  return result;
}
}
