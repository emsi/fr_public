// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <cfenv>
#include <cstring>
#include <iostream>
#include <numbers>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace original {
using sInt=int;using sU16=std::uint16_t;using sU32=std::uint32_t;using sU64=std::uint64_t;using sF32=float;
constexpr float sPI2F=2*std::numbers::pi_v<float>,sPIF=std::numbers::pi_v<float>;
constexpr int KC_BITMAP=1;
double sFSin(double x){return std::sin(x);}double sFCos(double x){return std::cos(x);}
double sFSqrt(double x){return std::sqrt(x);}double sFPow(double x,double p){return std::pow(x,p);}
int sFtol(float f){check(std::isfinite(f) && double(f)>=-2147483648.0 && double(f)<=2147483647.0,"original Bump nonfinite/overflow");return int(std::nearbyint(f));}
int sRange7fff(int n){return std::clamp(n,0,32767);}
sU64 GetColor64(sU32 c){const auto p=wz3::color64(c);return std::bit_cast<sU64>(wz3::Pixel{p[2],p[1],p[0],p[3]});}
void AddScale64(sU64& result,sU64& base,sU64& color,int fade) {
#if defined(__SSE2__)
  const auto a=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(base)),b=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(color));
  auto weight=_mm_srai_epi32(_mm_cvtsi32_si128(fade),1);weight=_mm_packs_epi32(weight,weight);
  weight=_mm_unpacklo_epi16(weight,weight);weight=_mm_unpacklo_epi16(weight,weight);
  result=std::bit_cast<sU64>(_mm_cvtsi128_si64(_mm_adds_epi16(a,_mm_slli_epi16(_mm_mulhi_epi16(b,weight),1))));
#else
  result=std::bit_cast<sU64>(wz3::add_scale64(std::bit_cast<wz3::Pixel>(base),std::bit_cast<wz3::Pixel>(color),fade));
#endif
}
struct GenBitmap {
  int XSize,YSize,ClassId=KC_BITMAP;std::vector<sU64> storage;sU64* Data;
  explicit GenBitmap(const wz3::Texture& t):XSize(t.width),YSize(t.height) {
    for(const auto& p:t.pixels)storage.push_back(std::bit_cast<sU64>(wz3::Pixel{p[2],p[1],p[0],p[3]}));Data=storage.data();
  }
  void Release(){}
};
bool CheckBitmap(GenBitmap* out,GenBitmap** in){*in=out;return false;}
#define __stdcall
#define NOTEXTURES2(a,b)
#define SCRIPTVERIFY(x) check(bool(x),"original Bump precondition")
#include "bitmap_bump_oracle.inc"
#undef SCRIPTVERIFY
#undef NOTEXTURES2
#undef __stdcall
std::vector<wz3::Pixel> render(const wz3::Texture& texture,const wz3::Texture* normals,std::span<const std::uint32_t> p) {
  GenBitmap a(texture),b(normals ? *normals : texture);const auto r=[&](unsigned i){return wz3::as_float(p[i]);};
  Bitmap_Bump(&a,normals ? &b : nullptr,p[0],r(1),r(2),r(3),r(4),r(5),p[6],p[7],r(8),r(9),r(10),p[11],r(12),r(13));
  std::vector<wz3::Pixel> out;for(auto v:a.storage){auto pixel=std::bit_cast<wz3::Pixel>(v);std::swap(pixel[0],pixel[2]);out.push_back(pixel);}return out;
}
}
namespace {
template<class F> void rejects(F action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}check(rejected,"invalid Bump input accepted");}
void compare(const wz3::Texture& source,const wz3::Texture* normals,std::span<const std::uint32_t> p) {
  auto actual=source;wz3::bump_bitmap(actual,normals,p);
  const auto expected=original::render(source,normals,p);
  for(std::size_t i=0;i<expected.size();++i)for(unsigned c=0;c<4;++c)if(actual.pixels[i][c]!=expected[i][c])
    throw std::runtime_error("Bump differs from archived loop at pixel "+std::to_string(i)+" lane "+std::to_string(c)+": expected "+std::to_string(expected[i][c])+", got "+std::to_string(actual.pixels[i][c]));
}
void synthetic() {
  std::uint32_t random=16;const auto next=[&](){return random=random*1664525+1013904223;};
  for(unsigned i=0;i<10000;++i) {
    wz3::Pixel a,b;for(auto& x:a)x=next()>>16;for(auto& x:b)x=next()>>16;const auto fade=std::bit_cast<std::int32_t>(next());
    auto aa=std::bit_cast<std::uint64_t>(a),bb=std::bit_cast<std::uint64_t>(b);std::uint64_t result;
    original::AddScale64(result,aa,bb,fade);check(wz3::add_scale64(a,b,fade)==std::bit_cast<wz3::Pixel>(result),"AddScale64 differs from MMX instruction sequence");
  }
  wz3::Texture source;source.width=16;source.height=8;source.pixels.resize(128);auto normals=source;
  for(auto& p:source.pixels)for(auto& c:p)c=(next()>>16)&32767;
  for(auto& p:normals.pixels)for(auto& c:p)c=(next()>>16)&32767;
  std::vector<std::uint32_t> p={0,f(.4f),f(.35f),f(.8f),f(.14f),f(.3f),0xff884422,0x80302010,f(.5f),f(2.5f),f(1.25f),0xaacc4499,f(8.5f),f(.7f)};
  unsigned cases=0;
  for(unsigned mode=0;mode<3;++mode)for(bool bump:{false,true})for(float amplitude:{-2.f,0.f,.5f,3.f})for(float specular:{0.f,.5f,2.f}) {
    p[0]=mode;p[10]=f(amplitude);p[13]=f(specular);compare(source,bump ? &normals : nullptr,p);++cases;
  }
  p[0]=2;const auto previous=std::fegetround();std::fesetround(FE_DOWNWARD);auto copy=source;wz3::bump_bitmap(copy,&normals,p);check(std::fegetround()==FE_DOWNWARD,"Bump leaked rounding mode");std::fesetround(previous);
  compare(source,&normals,p);check(copy.pixels==original::render(source,&normals,p),"Bump inherited caller rounding mode");
  auto bad=p;bad[0]=3;rejects([&]{wz3::bump_bitmap(copy,nullptr,bad);});bad=p;bad[8]=0;rejects([&]{wz3::bump_bitmap(copy,nullptr,bad);});bad=p;bad.pop_back();rejects([&]{wz3::bump_bitmap(copy,nullptr,bad);});
  auto small=normals;small.width=4;small.pixels.resize(32);rejects([&]{wz3::bump_bitmap(copy,&small,p);});
  FrameFixture fixture;
  const auto base_id=fixture.add(0x21,{4,3,0xc0804020}),height=fixture.add(0x34,{4,3,0xff000000,0xffffffff,f(.11f),f(.5f),f(1),0});
  const auto normal_id=fixture.add(0x2e,{f(.5f),1},{height});
  for(bool bump:{false,true}) {
    const auto parameters=bump ? p : std::vector<std::uint32_t>(p.begin(),p.begin()+11);
    const auto id=fixture.add(bump ? 0x30 : 0x2f,parameters,bump ? std::vector<std::size_t>{base_id,normal_id} : std::vector<std::size_t>{base_id});
    const auto base=wz3::evaluate_texture(fixture.p,fixture.g,base_id),n=wz3::evaluate_texture(fixture.p,fixture.g,normal_id),actual=wz3::evaluate_texture(fixture.p,fixture.g,id);
    auto expanded=parameters;if(!bump)expanded.insert(expanded.end(),{expanded[7],0,0});
    check(actual->pixels==original::render(*base,bump ? n.get() : nullptr,expanded),"Light/Bump evaluator input wiring");
  }
  std::cout<<cases<<" Bump/Light cases and 10000 MMX packed arithmetic cases matched\n";
}
void masks() {
  std::uint32_t state=91;const auto random=[&](){return state=state*1664525+1013904223;};
  for(unsigned n=0;n<4096;++n) {
    wz3::Pixel mask,a,b;for(auto& c:mask)c=random()>>16;for(auto& c:a)c=random()>>16;for(auto& c:b)c=random()>>16;
    for(unsigned mode=0;mode<4;++mode) {
#if defined(__SSE2__)
      // Bitmap_Inner loop8 (gray) followed by loops b, 2+0, 2+d or e.
      const wz3::Pixel bgra={mask[2],mask[1],mask[0],mask[3]};
      auto m=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(bgra.data()));const auto g=_mm_srli_si128(m,2),r=_mm_srli_si128(m,4);
      m=_mm_srli_epi16(_mm_add_epi16(_mm_srli_epi16(_mm_add_epi16(m,g),1),_mm_srli_epi16(_mm_add_epi16(r,g),1)),1);
      m=_mm_unpacklo_epi16(m,m);m=_mm_unpacklo_epi16(m,m);m=_mm_or_si128(m,_mm_set_epi16(0,0,0,0,32767,0,0,0));
      auto x=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(a.data())),y=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(b.data()));__m128i out;
      if(mode==1 || mode==2) {
        const auto mul=_mm_slli_epi16(_mm_mulhi_epi16(m,x),1);out=mode==1 ? _mm_adds_epi16(mul,y) : _mm_subs_epu16(y,mul);
      }else {
        if(mode==3)x=_mm_slli_epi16(_mm_mulhi_epi16(x,y),1);
        const auto inverse=_mm_subs_epi16(_mm_set1_epi16(32767),m);
        out=_mm_slli_epi16(_mm_adds_epi16(_mm_mulhi_epi16(m,x),_mm_mulhi_epi16(inverse,y)),1);
      }
      wz3::Pixel expected;_mm_storel_epi64(reinterpret_cast<__m128i*>(expected.data()),out);
      check(wz3::mask_pixel(mask,a,b,mode)==expected,"Mask differs from archived MMX instructions");
#else
      (void)wz3::mask_pixel(mask,a,b,mode);
#endif
    }
  }
  FrameFixture fixture;const auto mask=fixture.add(0x21,{2,2,0x80804020}),a=fixture.add(0x21,{2,2,0xffff0000}),b=fixture.add(0x21,{2,2,0xff0000ff});
  for(unsigned mode=0;mode<4;++mode) {
    const auto id=fixture.add(0x2a,{mode},{mask,a,b});const auto t=wz3::evaluate_texture(fixture.p,fixture.g,id);
    const auto expected=wz3::mask_pixel(wz3::color64(0x80804020),wz3::color64(0xffff0000),wz3::color64(0xff0000ff),mode);
    check(std::all_of(t->pixels.begin(),t->pixels.end(),[&](const auto& p){return p==expected;}),"Mask evaluator wiring");
  }
  rejects([&]{wz3::mask_pixel({}, {}, {},4);});
  std::cout<<"16384 Mask cases matched archived MMX instruction sequences\n";
}
void debris(const char* path) {
  const auto project=wz3::read_project(path);const auto graph=wz3::connect(project);unsigned recipes=0;
  wz3::Texture texture;texture.width=8;texture.height=4;texture.pixels.resize(32,wz3::color64(0xc0804020));
  auto normals=texture;for(unsigned i=0;i<32;++i)normals.pixels[i]={std::uint16_t(24576+i*32),std::uint16_t(16384+i*128),std::uint16_t(8192+i*64),65535};
  for(std::size_t id=0;id<project.operators.size();++id)if(graph.nodes[id].reachable && (project.operators[id].command==0x2f || project.operators[id].command==0x30)) {
    const auto& op=project.operators[id];auto p=op.parameters;if(op.command==0x2f)p.insert(p.end(),{p[7],0,0});
    compare(texture,graph.nodes[id].inputs.size()==2 ? &normals : nullptr,p);++recipes;
  }
  std::cout<<recipes<<" reachable saved Light/Bump parameter sets matched with controlled source textures\n";
}
}
int main(int argc,char** argv){try{synthetic();masks();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
