// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Bitmap_Bricks/Bitmap_Bulge from ../genbitmap.cpp.
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <stdexcept>

namespace wz3 {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Nearest {std::fenv_t previous;Nearest(){std::fegetenv(&previous);std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetenv(&previous);}};
int integer(double x){require(std::isfinite(x) && x>=-2147483648. && x<=2147483647.,"bitmap pattern arithmetic exceeds int32 range");return int(x);}
void validate(const Texture& t){require(std::has_single_bit(t.width) && std::has_single_bit(t.height) && t.width<=4096 && t.height<=4096 && t.pixels.size()==std::size_t(t.width)*t.height,"invalid bitmap pattern dimensions");}
class Random {
  std::uint32_t state;
public:
  explicit Random(std::uint32_t seed) {
    const auto product=seed*121u,mixed=seed+seed*17u+product+std::uint32_t(std::bit_cast<std::int32_t>(product)/17);
    state=mixed;for(unsigned i=0;i<4;++i){next();if(i!=3)state^=mixed;}
  }
  std::uint32_t next(){const auto first=state*0x343fdu+0x269ec3u;state=first*0x343fdu+0x269ec3u;return ((state>>10)&65535)|((first<<6)&0xffff0000u);}
  float real(){return ((next()&0x3fffffffu)*1.f)/0x40000000;}
};
}
void bricks_bitmap(Texture& texture,std::span<const std::uint32_t> p) {
  const Nearest rounding;
  validate(texture);
  require(p.size()==14,"invalid Bricks parameters");
  const auto real=[&](unsigned i){const auto v=as_float(p[i]);require(std::isfinite(v),"non-finite Bricks parameter");return v;};
  const unsigned flags=p[11],multiply=1u<<((flags>>4)&7),base_x=p[7],base_y=p[8];
  require(base_x>=1 && base_y>=1 && base_x<=32768/multiply && base_y<=32768/multiply && (!(flags&4) || base_x>=3),"invalid Bricks cell dimensions");
  const unsigned nx=base_x*multiply,ny=base_y*multiply;
  require(std::size_t(nx)*ny<=1048576,"Bricks cell budget exceeded");
  const int gap_x=integer(real(5)*8192.f),gap_y=integer(real(6)*8192.f),step=integer(real(12)*16384.f);
  const auto balance=real(13);const int heads=std::bit_cast<std::int32_t>(p[10]);Random random(p[9]);
  const auto c0=color64(p[2]),c1=color64(p[3]),mortar=color64(p[4]);
  struct Cell {Pixel color{};bool head{};};std::vector<Cell> cells(std::size_t(nx)*ny);
  bool head=false;
  for(unsigned y=0;y<base_y;++y) {
    auto* row=cells.data()+y*nx;int trigger=0;
    for(unsigned x=0;x<base_x;++x) {
      --trigger;row[x].head=head;
      if(!head)head=true;
      else if(int(random.next()%255)>heads || trigger>=0)head=false;
      else if(flags&4)trigger=2;
    }
    if(!row[0].head && !row[base_x-1].head)row[0].head=true;
    if(flags&4) {
      if(row[0].head && row[1].head && row[2].head)row[1].head=false;
      if(row[base_x-1].head && row[0].head && row[1].head)row[0].head=false;
      if(row[base_x-2].head && row[base_x-1].head && row[0].head)row[base_x-1].head=false;
    }
  }
  for(unsigned yy=0;yy<multiply;++yy)for(unsigned y=0;y<base_y;++y)
    for(unsigned xx=0;xx<multiply;++xx)for(unsigned x=0;x<base_x;++x)
      cells[(yy*base_y+y)*nx+xx*base_x+x]=cells[y*nx+x];
  const auto color=[&](){return fade64(c0,c1,integer(std::pow(double(random.real()),double(balance))*65536));};
  auto brick=color();
  for(unsigned y=0;y<ny;++y) {
    auto* row=cells.data()+y*nx;
    for(unsigned x=0;x<nx;++x){if(row[x].head)brick=color();row[x].color=brick;}
    if(!row[0].head)row[0].color=row[nx-1].color;
  }
  const int pixel_x=16384/texture.width*nx,pixel_y=16384/texture.height*ny;
  // Preserve 32-bit wrapping before the original signed division.
  const auto ramp=[](int value,int divisor){return std::bit_cast<std::int32_t>(std::uint32_t(value)*16384u)/divisor;};
  for(unsigned y=0;y<texture.height;++y) {
    const int row_y=16384/texture.height*y*ny,by=row_y>>14,fy=row_y&16383;
    const auto offset=(std::uint32_t(by)*std::uint32_t(step))&32767;
    for(unsigned x=0;x<texture.width;++x) {
      const int row_x=16384/texture.width*x*nx+offset,bx=(row_x>>14)%nx,fx=row_x&16383;
      const auto* row=cells.data()+by*nx;int value=16384;
      const auto edge=[&](int distance,int gap,int pixel) {return flags&8 ? ramp(std::max(0,distance-(gap-pixel)),pixel) : ramp(distance,gap);};
      if(row[bx].head && fx<gap_x)value=edge(fx,gap_x,pixel_x);
      if(row[(bx+1)%nx].head && 16384-fx<gap_x)value=edge(16384-fx,gap_x,pixel_x);
      if(fy<gap_y)value=std::min(value,edge(fy,gap_y,pixel_y));
      if(16384-fy<gap_y)value=std::min(value,edge(16384-fy,gap_y,pixel_y));
      texture.pixels[std::size_t(y)*texture.width+x]=fade64(mortar,row[bx].color,value*4);
    }
  }
}
void bulge_bitmap(const Texture& source,Texture& destination,float warp) {
  const Nearest rounding;validate(source);validate(destination);
  require(&source!=&destination && source.width==destination.width && source.height==destination.height,"Bulge requires separate, equally sized textures");
  require(std::isfinite(warp),"non-finite Bulge warp");
  const float inv_x=1.f/source.width,inv_y=1.f/source.height;
  const int scale_x=source.width<<16,scale_y=source.height<<16;
  for(unsigned y=0;y<source.height;++y) {
    const float fy=y*inv_y,center_y=(.5f-fy)*2;
    for(unsigned x=0;x<source.width;++x) {
      const float fx=x*inv_x;float u=(fx-.5f)*2,v=center_y;
      const float radius=u*u+v*v;
      if(radius<=1) {
        const float inverse=1.f/(1.f+warp*std::sqrt(double(1.f-radius)));
        u*=inverse;v*=inverse;
      }
      u=(1.f+u)*.5f;v=(1.f-v)*.5f;
      destination.pixels[std::size_t(y)*source.width+x]=sample_bitmap(source,integer(u*scale_x),integer(v*scale_y),0);
    }
  }
}
} // namespace wz3
