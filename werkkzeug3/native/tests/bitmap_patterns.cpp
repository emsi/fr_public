// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <cfenv>
#include <iostream>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace original {
using sInt=int;using sF32=float;using sU16=std::uint16_t;using sU32=std::uint32_t;using sU64=std::uint64_t;
sU32 sRandomSeed;
template<class T>T sMin(T a,T b){return std::min(a,b);}template<class T>T sMax(T a,T b){return std::max(a,b);}
double sFPow(double a,double b){return std::pow(a,b);}double sFSqrt(double a){return std::sqrt(a);}
sU64 pack(wz3::Pixel p){std::swap(p[0],p[2]);return std::bit_cast<sU64>(p);}
wz3::Pixel unpack(sU64 p){auto result=std::bit_cast<wz3::Pixel>(p);std::swap(result[0],result[2]);return result;}
struct GenBitmap {
  int XSize,YSize,Size;sU64* Data;
  GenBitmap(int x,int y):XSize(x),YSize(y),Size(x*y),Data(new sU64[Size]){}
  explicit GenBitmap(const wz3::Texture& t):GenBitmap(t.width,t.height){for(int i=0;i<Size;++i)Data[i]=pack(t.pixels[i]);}
  ~GenBitmap(){delete[] Data;}
  std::vector<wz3::Pixel> pixels()const {std::vector<wz3::Pixel> result;for(int i=0;i<Size;++i)result.push_back(unpack(Data[i]));return result;}
};
GenBitmap* NewBitmap(int x,int y){return new GenBitmap(1<<std::clamp(x&127,0,12),1<<std::clamp(y,0,12));}
bool CheckBitmap(GenBitmap* bm,GenBitmap** input){*input=bm;return false;}
struct BilinearContext {wz3::Texture texture;unsigned flags;};
void BilinearSetup(BilinearContext* context,sU64* source,int w,int h,int border) {
  context->texture.width=w;context->texture.height=h;context->flags=border;
  for(int i=0;i<w*h;++i)context->texture.pixels.push_back(unpack(source[i]));
}
void BilinearFilter(BilinearContext* c,sU64* output,int u,int v){*output=pack(wz3::sample_bitmap(c->texture,u,v,c->flags));}
void Fade64(sU64& d,sU64 a,sU64 b,int fade) {
#if defined(__SSE2__)
  auto f=_mm_srai_epi32(_mm_cvtsi32_si128(fade),1),g=_mm_sub_epi32(_mm_set1_epi32(32768),f);
  f=_mm_packs_epi32(f,f);g=_mm_packs_epi32(g,g);
  f=_mm_unpacklo_epi16(f,f);f=_mm_unpacklo_epi16(f,f);g=_mm_unpacklo_epi16(g,g);g=_mm_unpacklo_epi16(g,g);
  const auto x=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(a)),y=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(b));
  d=std::bit_cast<sU64>(_mm_cvtsi128_si64(_mm_slli_epi16(_mm_add_epi16(_mm_mulhi_epi16(x,g),_mm_mulhi_epi16(y,f)),1)));
#else
  d=pack(wz3::fade64(unpack(a),unpack(b),fade));
#endif
}
#define sMOBILE 1
#define NOTEXTURES1(a)
#define __stdcall
#define sVERIFY(x) check(x,"invalid original pattern")
#include "bitmap_patterns_oracle.inc"
#undef sVERIFY
#undef __stdcall
#undef NOTEXTURES1
#undef sMOBILE
std::vector<wz3::Pixel> bricks(std::span<const std::uint32_t> p) {
  const auto r=[&](unsigned i){return wz3::as_float(p[i]);};
  std::unique_ptr<GenBitmap> result(Bitmap_Bricks(p[0],p[1],p[2],p[3],p[4],r(5),r(6),p[7],p[8],p[9],p[10],p[11],r(12),r(13)));
  return result->pixels();
}
std::vector<wz3::Pixel> bulge(const wz3::Texture& source,float warp) {GenBitmap bitmap(source);Bitmap_Bulge(&bitmap,warp);return bitmap.pixels();}
}
namespace {
template<class F> void rejects(F action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}check(rejected,"invalid bitmap pattern accepted");}
void compare_bricks(const std::vector<std::uint32_t>& p) {
  FrameFixture fixture;const auto id=fixture.add(0x3d,p);const auto actual=wz3::evaluate_texture(fixture.p,fixture.g,id);
  check(actual->pixels==original::bricks(p),"Bricks differs from archived handler");
}
void synthetic() {
  std::vector<std::uint32_t> p={6,5,0x23687a9b,0xfab29467,0x95766435,f(.2f),f(.4f),7,5,0,80,0,f(.25f),f(1)};unsigned count=0;
  for(unsigned flags:{0u,4u,8u,12u,22u,46u,54u,70u,118u})for(unsigned seed:{0u,1u,0x7fffffffu,0xdeadbeefu})
    for(float balance:{0.f,.25f,1.f,3.f})for(float gap:{0.f,.17f,.8f}) {
      p[9]=seed;p[11]=flags;p[13]=f(balance);p[5]=f(gap);p[6]=f(gap*.3f);p[12]=f(count%2 ? -.6f : .25f);compare_bricks(p);++count;
    }
  p[0]=1;p[1]=0;p[7]=1;p[8]=1;p[11]=0;compare_bricks(p);++count;
  std::cout<<count<<" Bricks pattern, color, mortar, seed and repetition cases matched archived handler\n";
  FrameFixture fixture;const auto image=fixture.add(0x34,{6,5,0x18203844,0xeeb093fe,f(.17f),f(.3f),f(1.1f),0});
  const auto formatted=fixture.add(0x25,{1,3,17},{image});const auto source=wz3::evaluate_texture(fixture.p,fixture.g,formatted);
  for(float warp:{-3.f,-.99f,-.5f,0.f,.01f,.5f,1.f,4.f}) {
    const auto id=fixture.add(0x3e,{f(warp)},{formatted});const auto actual=wz3::evaluate_texture(fixture.p,fixture.g,id);
    check(actual->pixels==original::bulge(*source,warp),"Bulge differs from archived handler");
    check(actual->mip_count==3 && actual->mip_threshold==17,"Bulge metadata");
  }
  auto output=*source;
  rejects([&]{wz3::bulge_bitmap(*source,output,-1);});rejects([&]{wz3::bulge_bitmap(output,output,1);});
  p[7]=0;rejects([&]{wz3::bricks_bitmap(output,p);});p[7]=1;p[11]=4;rejects([&]{wz3::bricks_bitmap(output,p);});
  p[7]=p[8]=32768;p[11]=0;rejects([&]{wz3::bricks_bitmap(output,p);});
  p={6,5,0x23687a9b,0xfab29467,0x95766435,f(.2f),f(.4f),7,5,0,80,0,f(.25f),f(.7f)};
  wz3::bricks_bitmap(output,p);const auto expected=output.pixels;
  const auto saved=std::fegetround();std::fesetround(FE_DOWNWARD);wz3::bricks_bitmap(output,p);
  check(std::fegetround()==FE_DOWNWARD,"Bricks failed to restore rounding");std::fesetround(saved);
  check(output.pixels==expected,"Bricks depends on caller rounding");
  std::cout<<"Bulge coordinates, wrapped bilinear sampling, metadata and invalid inputs checked\n";
}
void debris(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);unsigned bricks=0,bulges=0;
  wz3::Texture source;source.width=32;source.height=16;source.pixels.resize(512);
  for(unsigned i=0;i<512;++i)source.pixels[i]={std::uint16_t(i*53%32768),std::uint16_t(i*83%32768),std::uint16_t(i*131%32768),std::uint16_t(i*197)};
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable) {
    const auto& op=p.operators[id];
    if(op.command==0x3d){compare_bricks(op.parameters);++bricks;}
    else if(op.command==0x3e) {
      auto result=source;const auto warp=wz3::as_float(op.parameters[0]);wz3::bulge_bitmap(source,result,warp);
      check(result.pixels==original::bulge(source,warp),"saved Bulge differs");
      ++bulges;
    }
  }
  check(bricks==36 && bulges==1,"Debris pattern recipe counts changed");
  std::cout<<bricks<<" full-resolution Debris Bricks images matched archived handler\n";
  std::cout<<bulges<<" saved Bulge parameter set matched with a controlled input; its real Text dependency is not yet ported\n";
}
}
int main(int argc,char** argv){try{synthetic();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
