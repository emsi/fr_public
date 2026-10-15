// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "noise.hpp"
#include <algorithm>
#include <cfenv>
#include <cstring>
#include <iostream>
#include <numbers>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace original {
using sInt=int;using sF32=float;using sU32=std::uint32_t;using sU64=std::uint64_t;using sU8=std::uint8_t;using sChar=char;
constexpr float sPI2F=2*std::numbers::pi_v<float>;
sU32 sRandomSeed;
float gradient3[64],sPerlinRandom[256][2];sU8 sPerlinPermute[512];int GammaTable[1025];
template<class T> void sSwap(T& a,T& b){std::swap(a,b);}template<class T> T sMin(T a,T b){return std::min(a,b);}
void sCopyMem(void* d,const void* s,std::size_t n){std::memcpy(d,s,n);}void sSetMem(void* d,int v,std::size_t n){std::memset(d,v,n);}
double sFPow(double a,double b){return std::pow(a,b);}double sFSin(double v){return std::sin(v);}
int sGetPower2(unsigned n){return std::countr_zero(n);}int sAbs(int x){return x<0 ? -x : x;}
int sFtol(float v){check(std::isfinite(v) && double(v)>=-2147483648. && double(v)<=2147483647.,"invalid reference Perlin FISTP");return int(std::nearbyint(v));}
int sRange7fff(int v){return std::clamp(v,0,32767);}
int sMulShift(int a,int b){return std::bit_cast<std::int32_t>(std::uint32_t((std::int64_t(a)*b)>>16));}
struct GenBitmap {
  int XSize,YSize;std::vector<sU64> pixels;sU64* Data;
  GenBitmap(int w,int h):XSize(w),YSize(h),pixels(std::size_t(w)*h),Data(pixels.data()){}
};
GenBitmap* NewBitmap(int x,int y){return new GenBitmap(1<<std::clamp(x&127,0,12),1<<std::clamp(y,0,12));}
void Fade64(sU64& d,sU64 a,sU64 b,int fade) {
#if defined(__SSE2__)
  auto f=_mm_srai_epi32(_mm_cvtsi32_si128(fade),1),g=_mm_sub_epi32(_mm_set1_epi32(32768),f);
  f=_mm_packs_epi32(f,f);g=_mm_packs_epi32(g,g);
  f=_mm_unpacklo_epi16(f,f);f=_mm_unpacklo_epi16(f,f);g=_mm_unpacklo_epi16(g,g);g=_mm_unpacklo_epi16(g,g);
  const auto x=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(a)),y=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(b));
  d=std::bit_cast<sU64>(_mm_cvtsi128_si64(_mm_slli_epi16(_mm_add_epi16(_mm_mulhi_epi16(x,g),_mm_mulhi_epi16(y,f)),1)));
#else
  d=std::bit_cast<sU64>(wz3::fade64(std::bit_cast<wz3::Pixel>(a),std::bit_cast<wz3::Pixel>(b),fade));
#endif
}
#define sMOBILE 1
#define sINTRO 0
#define NOTEXTURES0()
#define __stdcall
#define __assume(x) check(x,"reference Perlin unreachable branch")
#include "bitmap_perlin_oracle.inc"
#undef __assume
#undef __stdcall
#undef NOTEXTURES0
#undef sINTRO
#undef sMOBILE
std::vector<wz3::Pixel> render(std::span<const std::uint32_t> p) {
  const auto r=[&](unsigned i){return wz3::as_float(p[i]);};
  std::unique_ptr<GenBitmap> bm(Bitmap_Perlin(p[0],p[1],p[2],p[3],r(4),p[5],p[6],r(7),r(8),p[9],p[10]));
  std::vector<wz3::Pixel> out;out.reserve(bm->pixels.size());
  for(auto pixel:bm->pixels){auto value=std::bit_cast<wz3::Pixel>(pixel);std::swap(value[0],value[2]);out.push_back(value);}return out;
}
}
namespace {
template<class F> void rejects(F action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}check(rejected,"invalid Perlin input accepted");}
void compare(std::vector<std::uint32_t> p) {
  FrameFixture fixture;const auto id=fixture.add(0x22,p);const auto actual=wz3::evaluate_texture(fixture.p,fixture.g,id);const auto expected=original::render(p);
  check(actual->pixels.size()==expected.size(),"Perlin dimensions differ");
  for(std::size_t i=0;i<expected.size();++i)for(unsigned c=0;c<4;++c)if(actual->pixels[i][c]!=expected[i][c])throw std::runtime_error(
    "Perlin "+std::to_string(p[0])+"x"+std::to_string(p[1])+" frequency "+std::to_string(p[2])+" octaves "+std::to_string(p[3])+" mode "+std::to_string(p[6])+" differs at pixel "+std::to_string(i)+" channel "+std::to_string(c)+": expected "+std::to_string(expected[i][c])+", got "+std::to_string(actual->pixels[i][c]));
}
void synthetic() {
  original::sInitPerlin();
  check(std::equal(std::begin(original::sPerlinPermute),std::end(original::sPerlinPermute),wz3::legacy::perlin_permutation().begin()),"Perlin permutation differs from original initialization");
  for(unsigned i=0;i<256;++i)for(unsigned c=0;c<2;++c)check(original::sPerlinRandom[i][c]==wz3::legacy::perlin_gradients2()[i][c],"Perlin disk gradients differ from original initialization");
  std::vector<std::uint32_t> p={5,4,1,6,f(.7f),19,0,f(1.1f),f(.8f),0x18683bc7,0xf9e4ba62};unsigned cases=0;
  for(unsigned size:{0u,3u,5u,7u})for(unsigned mode=0;mode<4;++mode)for(float falloff:{-1.25f,0.f,.5f,1.f,1.5f})for(float gamma:{0.f,.3f,1.f,3.f}) {
    p[0]=size;p[1]=size ? size-1 : 0;p[2]=std::min(size,1u);p[4]=f(falloff);p[6]=mode;p[8]=f(gamma);compare(p);++cases;
  }
  // Tall images still have subpixel Y noise after X reaches its lattice.
  p={3,6,2,7,f(.65f),255,0,f(.9f),f(1.2f),0xff000000,0xffffffff};compare(p);
  p[0]|=128;p[5]=511;p[6]=4;compare(p); // saved width lock and masked seed/mode
  p[3]=0;compare(p);p[0]=p[1]=0;p[2]=p[3]=8;compare(p); // all integer coordinates
  p={5,5,1,6,f(.7f),19,0,f(1.1f),f(.8f),0x18683bc7,0xf9e4ba62};
  FrameFixture fixture;const auto id=fixture.add(0x22,p);std::fesetround(FE_DOWNWARD);std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
  const auto changed=wz3::evaluate_texture(fixture.p,fixture.g,id);check(std::fegetround()==FE_DOWNWARD && std::fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO,"Perlin failed to restore FP environment");
  std::fesetround(FE_TONEAREST);std::feclearexcept(FE_ALL_EXCEPT);check(changed->pixels==original::render(p),"Perlin depends on caller rounding");
  for(unsigned index:{2u,3u}){fixture.p.operators[id].parameters=p;fixture.p.operators[id].parameters[index]=17;rejects([&]{wz3::evaluate_texture(fixture.p,fixture.g,id);});}
  fixture.p.operators[id].parameters=p;fixture.p.operators[id].parameters[8]=f(-1);rejects([&]{wz3::evaluate_texture(fixture.p,fixture.g,id);});
  fixture.p.operators[id].parameters=p;fixture.p.operators[id].parameters.pop_back();rejects([&]{wz3::evaluate_texture(fixture.p,fixture.g,id);});
  fixture.p.operators[id].parameters={1,6,4,2,f(.5f),0,0,f(1),f(1),0,0xffffffff};rejects([&]{wz3::evaluate_texture(fixture.p,fixture.g,id);});
  std::cout<<cases+4<<" Perlin parameter combinations matched the archived editor kernel and tables\n";
}
void debris(const char* path) {
  const auto project=wz3::read_project(path);const auto graph=wz3::connect(project);unsigned count=0;
  for(std::size_t id=0;id<project.operators.size();++id)if(graph.nodes[id].reachable && project.operators[id].command==0x22) {
    const auto actual=wz3::evaluate_texture(project,graph,id);check(actual->pixels==original::render(project.operators[id].parameters),"saved Debris Perlin image differs");++count;
  }
  check(count==42,"Debris Perlin operator count changed");
  std::cout<<count<<" complete Debris Perlin images matched, at their saved 256/512-pixel resolutions\n";
}
}
int main(int argc,char** argv){try{synthetic();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
