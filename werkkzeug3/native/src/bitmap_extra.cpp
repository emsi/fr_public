// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Bitmap_ColorBalance, Bitmap_Distort and Bitmap_Sharpen from genbitmap.cpp.
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
int integer(double v){require(std::isfinite(v) && v>=-2147483648.0 && v<=2147483647.0,"bitmap filter arithmetic exceeds int32 range");return int(v);}
int lane(std::uint16_t v){return std::bit_cast<std::int16_t>(v);}
int saturate(int v){return std::clamp(v,-32768,32767);}
void validate(const Texture& t) {
  require(std::has_single_bit(t.width) && std::has_single_bit(t.height) && t.width<=4096 && t.height<=4096 && t.pixels.size()==std::size_t(t.width)*t.height,"invalid bitmap filter dimensions");
}
void same_size(const Texture& a,const Texture& b){validate(a);validate(b);require(a.width==b.width && a.height==b.height,"bitmap filter inputs have different dimensions");}
}
void balance_bitmap(Texture& texture,std::span<const std::uint32_t> p) {
  const Nearest rounding;validate(texture);require(p.size()==9,"invalid ColorBalance parameter layout");
  const auto real=[&](unsigned i){const auto v=as_float(p[i]);require(std::isfinite(v) && std::abs(v)<=1e6f,"invalid ColorBalance parameter");return v;};
  std::array<std::array<int,257>,3> tables;
  for(unsigned c=0;c<3;++c) {
    const auto shadows=real(c),midtones=real(c+3),highlights=real(c+6);constexpr float scale=100.f/255.f;
    const float power=std::pow(.5,double(shadows*.5f+midtones+highlights*.5f));
    const auto low=-std::min(shadows,0.f)*scale,high=1.f-std::max(highlights,0.f)*scale;
    require(high!=low,"singular ColorBalance range");const auto inv=1.f/(high-low);
    for(unsigned i=0;i<=256;++i) {
      const float input=i/256.f,clipped=input>=high ? high : input<=low ? low : input;
      const float value=std::pow(double((clipped-low)*inv),double(power));
      tables[c][i]=std::clamp(integer(value*32768.f),0,32767);
    }
  }
  for(auto& pixel:texture.pixels)for(unsigned c=0;c<3;++c) {
    const auto v=pixel[c];require(v<=32767,"ColorBalance requires 15-bit RGB channels");
    const auto& table=tables[c];const auto index=v>>7;
    pixel[c]=table[index]+(((table[index+1]-table[index])*(v&127))>>7);
  }
}
void distort_bitmap(const Texture& image,const Texture& displacement,Texture& destination,float amount,unsigned border) {
  const Nearest rounding;validate(image);same_size(displacement,destination);
  require(image.pixels.size()==displacement.pixels.size(),"Distort inputs have different pixel counts");
  require(&destination!=&image && &destination!=&displacement,"Distort requires a separate destination");
  // The archived handler checks pixel count, then interprets both buffers
  // using the displacement bitmap's row stride, even for different shapes.
  Texture reshaped;const Texture* sampled=&image;
  if(image.width!=displacement.width) {
    reshaped=image;reshaped.width=displacement.width;reshaped.height=displacement.height;sampled=&reshaped;
  }
  const auto bx=integer((amount*displacement.width)*4),by=integer((amount*displacement.height)*4);
  for(unsigned y=0;y<displacement.height;++y)for(unsigned x=0;x<displacement.width;++x) {
    const auto index=std::size_t(y)*displacement.width+x;const auto& p=displacement.pixels[index];
    const auto u=(x<<16)+std::uint32_t(int(p[2])-16384)*std::uint32_t(bx);
    const auto v=(y<<16)+std::uint32_t(int(p[1])-16384)*std::uint32_t(by);
    // This operator always called BilinearFilter; bit 2 never selected point
    // filtering, unlike Bitmap_Rotate. Blue/green hold X/Y displacement.
    destination.pixels[index]=sample_bitmap(*sampled,std::bit_cast<std::int32_t>(u),std::bit_cast<std::int32_t>(v),border&3);
  }
}
void sharpen_bitmap(Texture& blurred,const Texture& original,float amplitude) {
  const Nearest rounding;same_size(blurred,original);
  const auto gain=std::clamp(integer(amplitude*2048.f),-32767,32767);
  for(std::size_t i=0;i<blurred.pixels.size();++i)for(unsigned c=0;c<4;++c) {
    // Original psubusw, signed 16x16 multiply, psrad 11, packssdw, paddsw.
    const auto delta=std::uint16_t(std::max(0,int(original.pixels[i][c])-int(blurred.pixels[i][c])));
    const auto product=(lane(delta)*gain)>>11;
    blurred.pixels[i][c]=std::max(0,saturate(lane(original.pixels[i][c])+saturate(product)));
  }
}
} // namespace wz3
