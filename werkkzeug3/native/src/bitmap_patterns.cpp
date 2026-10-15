// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Bitmap_Bricks/Bitmap_Bulge and editor Bitmap_Cell from ../genbitmap.cpp.
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
void cell_bitmap(Texture& texture,std::span<const std::uint32_t> p) {
  const Nearest rounding;validate(texture);
  require(texture.width>=16 && texture.height>=16,"editor Cell requires complete 16x16 tiles");
  require(p.size()==13 && p[5]>=1 && p[5]<=256,"invalid Cell parameter layout/count");
  const auto real=[&](unsigned i){const auto v=as_float(p[i]);require(std::isfinite(v),"non-finite Cell parameter");return v;};
  const auto amplitude=real(7),gamma=real(8);const unsigned mode=p[9];
  const int percent=std::bit_cast<std::int32_t>(p[11]);Random random(p[6]);
  struct Cell {int x,y,color;};std::array<Cell,256> cells;
  int count=p[5];
  for(int i=0;i<count;++i)cells[i]={int(random.next()%16384),int(random.next()%16384),int(random.next()%16384)};
  const int distance=integer(std::nearbyint(double(real(10)*16384.f)));
  const auto minimum=std::bit_cast<std::int32_t>(std::uint32_t(distance)*std::uint32_t(distance));
  for(int i=1;i<count;) {
    if((mode&2) && int(random.next()%255)<percent)cells[i].color=65535;
    const int px=(cells[i].x&16383)-8192,py=(cells[i].y&16383)-8192;bool cut=false;
    for(int j=0;j<i && !cut;++j) {
      const int dx=((cells[j].x-px)&16383)-8192,dy=((cells[j].y-py)&16383)-8192;
      cut=dx*dx+dy*dy<minimum;
    }
    if(cut)cells[i]=cells[--count];else ++i;
  }
  const float aspect=std::pow(2.,double(real(12)));
  require(std::isfinite(aspect) && aspect>0,"invalid Cell aspect ratio");
  const bool flip=aspect<1;
  const int aspect_fixed=integer(flip ? aspect*aspect*65536 : 65536/(aspect*aspect));
  const float scale=flip ? 1.f/(16384.f*aspect) : aspect/16384.f;
  if(flip)for(int i=0;i<count;++i)std::swap(cells[i].x,cells[i].y);
  const auto color0=color64(p[3]),color1=color64(p[2]),background=color64(p[4]);
  const int shift_x=14-std::countr_zero(texture.width),shift_y=14-std::countr_zero(texture.height);
  const auto scaled=[&](int x){return int((std::int64_t(x)*aspect_fixed)>>16);};
  std::array<int,256> lower;
  for(unsigned by=0;by<texture.height;by+=16)for(unsigned bx=0;bx<texture.width;bx+=16) {
    int x0=bx<<shift_x,x1=(bx+15)<<shift_x,y0=by<<shift_y,y1=(by+15)<<shift_y;
    if(flip){std::swap(x0,y0);std::swap(x1,y1);}
    for(int i=0;i<count;++i) {
      int dx=((cells[i].x-x0)&16383)-8192,dy=((cells[i].x-x1)&16383)-8192;
      if((dx^dy)<=0)lower[i]=0;
      else {dx=std::min(std::abs(dx),std::abs(dy));lower[i]=scaled(dx*dx);}
      dx=((cells[i].y-y0)&16383)-8192;dy=((cells[i].y-y1)&16383)-8192;
      if((dx^dy)>0){dy=std::min(std::abs(dx),std::abs(dy));lower[i]+=dy*dy;}
    }
    // Sorting persists between tiles. Equal-distance ties preserve this order
    // and can select a different cell color if replaced with an unstable sort.
    for(int i=1;i<count;++i) {
      const auto cell=cells[i];const int bound=lower[i];int j=i;
      while(j && lower[j-1]>bound){cells[j]=cells[j-1];lower[j]=lower[j-1];--j;}
      cells[j]=cell;lower[j]=bound;
    }
    for(unsigned ty=0;ty<16;++ty)for(unsigned tx=0;tx<16;++tx) {
      int x=(bx+tx)<<shift_x,y=(by+ty)<<shift_y;if(flip)std::swap(x,y);
      int best=0x40000000,second=0x40000000,nearest=-1;
      for(int i=0;i<count && second>lower[i];++i) {
        const int dx=((cells[i].x-x)&16383)-8192,dy=((cells[i].y-y)&16383)-8192,dist=scaled(dx*dx)+dy*dy;
        if(dist<best){second=best;best=dist;nearest=i;}
        else if(dist>best && dist<second)second=dist;
      }
      require(nearest>=0,"Cell nearest-point search failed");
      float value=std::sqrt(double(best))*scale;
      if(mode&1){const float other=std::sqrt(double(second))*scale;value=value+other>.00001f ? (other-value)/(other+value) : 0;}
      int fade=std::clamp(integer(std::pow(double(value*amplitude),double(gamma))*32768),0,32767)*2;
      if(mode&4)fade=65536-fade;
      auto color=color0;
      if(mode&2)color=cells[nearest].color==65535 ? background : fade64(color0,color1,cells[nearest].color*4);
      texture.pixels[std::size_t(by+ty)*texture.width+bx+tx]=fade64(color,(mode&2) ? background : color1,fade);
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
