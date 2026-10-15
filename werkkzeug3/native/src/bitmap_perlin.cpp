// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Bitmap_Perlin's !sINTRO implementation from ../genbitmap.cpp. The small
// executable uses a different scalar loop; this is the saved editor-project
// profile, not a claim of equality with a recovered release executable.
#include "wz3/texture.hpp"
#include "noise.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace wz3 {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Nearest {
  std::fenv_t previous;
  Nearest(){require(std::fegetenv(&previous)==0,"cannot save Perlin floating-point environment");require(std::fesetround(FE_TONEAREST)==0,"cannot set Perlin rounding");}
  ~Nearest(){std::fesetenv(&previous);}
};
int integer(double x) {require(std::isfinite(x) && x>=-2147483648.0 && x<=2147483647.0,"Perlin arithmetic exceeds int32 range");return int(x);}
int rounded(float x){return integer(std::nearbyint(double(x)));}
int wrap(std::uint32_t x){return std::bit_cast<std::int32_t>(x);}
int add(int a,int b){return wrap(std::uint32_t(a)+std::uint32_t(b));}
int sub(int a,int b){return wrap(std::uint32_t(a)-std::uint32_t(b));}
int mul(int a,int b){return wrap(std::uint32_t(a)*std::uint32_t(b));}
}
void perlin_bitmap(Texture& texture,std::span<const std::uint32_t> p) {
  const Nearest rounding;
  const auto width=texture.width,height=texture.height;
  require(std::has_single_bit(width) && std::has_single_bit(height) && width<=4096 && height<=4096 && texture.pixels.size()==std::size_t(width)*height,"invalid Perlin texture dimensions");
  require(p.size()==11 && p[2]<=16 && p[3]<=16 && p[2]+p[3]<=16,"invalid Perlin frequency/octave range");
  const auto real=[&](unsigned i){const auto v=as_float(p[i]);require(std::isfinite(v) && std::abs(v)<=1e6f,"invalid Perlin numeric parameter");return v;};
  const unsigned freq=p[2],oct=p[3],seed=p[5]&255,mode=p[6]&3;
  const auto fadeoff=real(4),gamma=real(8);auto amplitude=real(7);require(gamma>=0,"negative Perlin gamma");
  const unsigned shiftx=16-std::countr_zero(width),shifty=16-std::countr_zero(height);
  const auto& permutation=legacy::perlin_permutation();const auto& gradients=legacy::perlin_gradients2();
  std::array<int,1025> gamma_table;
  for(unsigned i=0;i<gamma_table.size();++i)gamma_table[i]=std::clamp(integer(std::pow(double(i/1024.f),double(gamma))*32768),0,32767)*2;
  const int offset=(mode&1) ? 0 : 16384;amplitude*=((mode&1) ? 32768 : 16384);const auto amp=rounded(amplitude);
  std::array<int,257> sine{};
  if(mode&2)for(unsigned x=0;x<sine.size();++x)sine[x]=rounded(std::sin(double((2*std::numbers::pi_v<float>)*x/256.f))*.5*65536);
  std::vector<int> row(width),poly(width>>freq);
  for(std::size_t x=0;x<poly.size();++x){const float f=float(x)/poly.size();poly[x]=rounded(f*f*f*(10+f*(6*f-15))*16384.f);}
  const auto c0=color64(p[9]),c1=color64(p[10]);
  for(unsigned y=0;y<height;++y) {
    std::fill(row.begin(),row.end(),0);float scale=1;
    for(unsigned i=freq;i<freq+oct;++i) {
      const auto xshift=shiftx+i,yshift=shifty+i;
      const auto group_size=xshift<16 ? std::min(width,1u<<(16-xshift)) : 1;
      const auto groups=xshift<16 ? width>>(16-xshift) : width;
      const auto mask=((1u<<i)-1)&255,py=y<<yshift,vy=(py>>16)&mask;
      const auto dtx=wrap(1u<<xshift);const float ty=(py&65535)/65536.f;
      const float tyf=ty*ty*ty*(10+ty*(6*ty-15)),ty0f=ty*(1-tyf),ty1f=(ty-1)*tyf;
      const auto vy0=permutation[vy^seed],vy1=permutation[((vy+1)&mask)^seed];
      const auto si=rounded(scale*16384.f);
      if(xshift<16 || (py&65535)) {
        require(!poly.empty(),"Perlin frequency exceeds the editor's row interpolation table");
        unsigned at=0;
        for(unsigned vx=0;vx<groups;++vx) {
          const auto v00=permutation[(vx&mask)+vy0],v01=permutation[((vx+1)&mask)+vy0];
          const auto v10=permutation[(vx&mask)+vy1],v11=permutation[((vx+1)&mask)+vy1];
          const float h0=gradients[v00][0]+(gradients[v10][0]-gradients[v00][0])*tyf;
          const float h1=gradients[v01][0]+(gradients[v11][0]-gradients[v01][0])*tyf;
          const float v0=gradients[v00][1]*ty0f+gradients[v10][1]*ty1f;
          const float v1=gradients[v01][1]*ty0f+gradients[v11][1]*ty1f;
          int a=rounded(v0*65536.f),b=rounded((v1-h1)*65536.f);
          const auto da=rounded(h0*dtx),db=rounded(h1*dtx);
          for(unsigned x=0;x<group_size;++x) {
            const auto index=x<<(i-freq);require(index<poly.size(),"Perlin interpolation index exceeds row");
            auto n=add(a,mul(sub(b,a),poly[index])>>14);
            if(mode==1 && n<0)n=sub(0,n);
            if(mode==3)n&=32767;
            if(mode&2){const auto t=(n>>8)&255;n=add(sine[t],mul(sub(sine[t+1],sine[t]),n&255)>>8);}
            row[at]=add(row[at],mul(n,si)>>14);++at;a=add(a,da);b=add(b,db);
          }
        }
      }
      scale*=fadeoff;
    }
    for(unsigned x=0;x<width;++x) {
      const auto value=std::clamp(add(wrap(std::uint32_t((std::int64_t(row[x])*amp)>>16)),offset),0,32767);
      const auto index=value>>5,fade=gamma_table[index]+((gamma_table[index+1]-gamma_table[index])*(value&31)>>5);
      texture.pixels[std::size_t(y)*width+x]=fade64(c0,c1,fade);
    }
  }
}
} // namespace wz3
