// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Bitmap_Bump and AddScale64 from ../genbitmap.cpp. RGBA storage replaces the
// archived BGRA words; the lighting kernel still consumes normals as B,G,R.
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace wz3 {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Nearest {int saved=std::fegetround();Nearest(){std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetround(saved);}};
int rounded(float v) {
  const auto n=std::nearbyint(double(v));require(std::isfinite(n) && n>=-2147483648.0 && n<=2147483647.0,"Bump texture arithmetic exceeds int32 range");return int(n);
}
void validate(const Texture& t) {
  require(std::has_single_bit(t.width) && std::has_single_bit(t.height) && t.width<=4096 && t.height<=4096 && t.pixels.size()==std::size_t(t.width)*t.height,"invalid Bump texture dimensions");
}
}
Pixel add_scale64(const Pixel& base,const Pixel& color,std::int32_t fade) {
  const auto weight=std::clamp(fade>>1,-32768,32767);Pixel out;
  for(unsigned c=0;c<4;++c) {
    const auto product=(std::bit_cast<std::int16_t>(color[c])*weight)>>16;
    const auto doubled=std::bit_cast<std::int16_t>(std::uint16_t(product*2));
    out[c]=std::uint16_t(std::clamp(int(std::bit_cast<std::int16_t>(base[c]))+doubled,-32768,32767));
  }
  return out;
}
void bump_bitmap(Texture& texture,const Texture* normals,std::span<const std::uint32_t> p) {
  const Nearest rounding;validate(texture);require(p.size()==14 && p[0]<=2,"invalid Bump texture parameters");
  if(normals){validate(*normals);require(texture.width==normals->width && texture.height==normals->height,"Bump texture inputs have different dimensions");}
  const auto real=[&](unsigned i){const auto v=as_float(p[i]);require(std::isfinite(v) && std::abs(v)<=1e6f,"invalid Bump numeric parameter");return v;};
  const auto mode=p[0];const int xs=texture.width,ys=texture.height;
  float px=real(1),py=real(2),pz=real(3),da=real(4),db=real(5);
  const auto diffuse=color64(p[6]),ambient=color64(p[7]),specular=color64(p[11]);
  const auto outer=real(8),falloff=real(9),amp=real(10),power=real(12);auto spec_amp=real(13);
  require(outer!=0 && (mode!=0 || outer!=1),"singular Bump spotlight cone");
  px=px*xs*2-(xs/2);py=py*ys*2-(ys/2);pz=pz*xs;
  da*=2*std::numbers::pi_v<float>;db*=std::numbers::pi_v<float>;
  float dx=std::cos(double(db)),dy=dx,dz=std::sin(double(db));dx*=std::sin(double(da));dy*=std::cos(double(da));
  if(mode==0){px-=dx*pz/dz;py-=dy*pz/dz;}
  spec_amp*=65536;
  for(unsigned y=0;y<texture.height;++y)for(unsigned x=0;x<texture.width;++x) {
    float lx=dx,ly=dy,lz=dz;
    if(mode!=2) {
      lx=float(x)-px;ly=float(y)-py;lz=pz;
      const float inverse=1/std::sqrt(double(lx*lx+ly*ly+lz*lz));lx*=inverse;ly*=inverse;lz*=inverse;
    }
    float nx=0,ny=0,nz=1;
    const auto index=std::size_t(y)*texture.width+x;
    if(normals){const auto& b=normals->pixels[index];nx=(int(b[2])-0x4000)/16384.f;ny=(int(b[1])-0x4000)/16384.f;nz=(int(b[0])-0x4000)/16384.f;}
    const auto diffuse_factor=lx*nx+ly*ny+lz*nz;float specular_factor=0,spot=1;
    if(spec_amp) {
      const auto hx=lx,hy=ly,hz=lz+1;const float inverse=1/std::sqrt(double(hx*hx+hy*hy+hz*hz));
      specular_factor=hx*nx+hy*ny+hz*nz;if(specular_factor<0)specular_factor=0;
      specular_factor=std::pow(double(specular_factor*inverse),double(power));
    }
    if(mode==0){spot=lx*dx+ly*dy+lz*dz;spot=spot<outer ? 0 : std::pow(double((spot-outer)/(1-outer)),double(falloff));}
    const auto factor=spot*diffuse_factor*amp;Pixel value;
    for(unsigned c=0;c<4;++c)value[c]=std::clamp(rounded(texture.pixels[index][c]*(ambient[c]+diffuse[c]*factor)/32768),0,32767);
    texture.pixels[index]=add_scale64(value,specular,rounded(spot*specular_factor*spec_amp));
  }
}
} // namespace wz3
