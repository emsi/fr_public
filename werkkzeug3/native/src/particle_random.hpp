// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "mesh_kernel.hpp"
#include <bit>
namespace wz3 {
// The archive has one mutable random stream. Share it between runtime particle
// operators; startup/precalculation RNG consumption is not reconstructed yet.
class ParticleRandom {
  std::uint32_t state=0x74382381;
public:
  void seed(std::uint32_t seed) {
    const auto product=seed*121u;
    const auto mix=seed+seed*17u+product+std::uint32_t(std::bit_cast<std::int32_t>(product)/17);
    state=mix;for(unsigned i=0;i<4;++i) {if(i)state^=mix;next();}
  }
  std::uint32_t next() {
    const auto a=state*0x343fdu+0x269ec3u;state=a*0x343fdu+0x269ec3u;
    return ((state>>10)&0xffffu)|((a<<6)&0xffff0000u);
  }
  float real(float max=1) {return (float(next()&0x3fffffff)*max)/1073741824.0f;}
  legacy::sVector vector(bool reverse=false) {
    legacy::sVector v;
    if(reverse) {v.z=real()*2-1;v.y=real()*2-1;v.x=real()*2-1;}
    else {v.x=real(2)-1;v.y=real(2)-1;v.z=real(2)-1;}
    return v;
  }
  legacy::sVector sphere(bool reverse=false) {
    for(unsigned tries=0;tries<1024;++tries) {auto v=vector(reverse);if(v.Dot3(v)<=1)return v;}
    throw std::runtime_error("particle random rejection limit");
  }
};
} // namespace wz3
