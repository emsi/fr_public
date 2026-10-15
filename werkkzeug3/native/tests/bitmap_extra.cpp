// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <cfenv>
#include <iostream>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace original {
using sInt=int;using sF32=float;using sU16=std::uint16_t;using sU32=std::uint32_t;using sU64=std::uint64_t;using sF323=std::array<float,3>;
constexpr int KC_BITMAP=1,BI_GRAY=8,BI_INVERT=9,BI_RANGE=21,BI_SHARPEN=15;
int CBTable[3][257];
template<class T>T sMin(T a,T b){return a<b ? a : b;}template<class T>T sMax(T a,T b){return a>b ? a : b;}
template<class T>T sRange(T a,T hi,T lo){return a>=hi ? hi : a<=lo ? lo : a;}
int sRange7fff(int v){return sRange(v,32767,0);}double sFPow(double a,double b){return std::pow(a,b);}
wz3::Pixel unpack(sU64 p){auto x=std::bit_cast<wz3::Pixel>(p);std::swap(x[0],x[2]);return x;}
sU64 pack(wz3::Pixel p){std::swap(p[0],p[2]);return std::bit_cast<sU64>(p);}
struct GenBitmap {
  int XSize,YSize,Size,ClassId=KC_BITMAP;std::vector<sU64> storage;sU64* Data;
  explicit GenBitmap(const wz3::Texture& t):XSize(t.width),YSize(t.height),Size(t.pixels.size()) {for(auto p:t.pixels)storage.push_back(pack(p));Data=storage.data();}
  GenBitmap(const GenBitmap& b):XSize(b.XSize),YSize(b.YSize),Size(b.Size),storage(b.storage),Data(storage.data()){}
  wz3::Texture texture()const {wz3::Texture t;t.width=XSize;t.height=YSize;for(auto p:storage)t.pixels.push_back(unpack(p));return t;}
  void AddRef(){}void Release(){}
};
bool CheckBitmap(GenBitmap* bm,GenBitmap** input){*input=bm;return false;}
bool CheckBitmap(GenBitmap*& bm){bm=new GenBitmap(*bm);return false;}
void Bitmap_Inner(sU64* d,sU64* s,int count,int mode,sU64* x) {
  for(int i=0;i<count;++i) {
#if defined(__SSE2__)
    const auto input=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(x[i])),zero=_mm_setzero_si128(),mask=_mm_set1_epi16(32767);
    __m128i value;
    if(mode==BI_GRAY) {
      const auto g=_mm_srli_epi64(input,16),r=_mm_srli_epi64(input,32);
      value=_mm_srli_epi16(_mm_add_epi16(_mm_srli_epi16(_mm_add_epi16(input,g),1),_mm_srli_epi16(_mm_add_epi16(r,g),1)),1);
      value=_mm_unpacklo_epi16(value,value);value=_mm_unpacklo_epi16(value,value);value=_mm_or_si128(value,_mm_setr_epi16(0,0,0,32767,0,0,0,0));
    }else if(mode==BI_INVERT)value=_mm_xor_si128(input,mask);
    else if(mode==BI_RANGE) {
      const auto low=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(s[0])),high=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(s[1]));
      value=_mm_max_epi16(zero,_mm_adds_epi16(_mm_slli_epi16(_mm_mulhi_epi16(input,_mm_subs_epi16(high,low)),1),low));
    }else if(mode==BI_SHARPEN) {
      const auto blurred=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(d[i])),gain=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(s[0]));
      const auto delta=_mm_subs_epu16(input,blurred),lo=_mm_mullo_epi16(delta,gain),hi=_mm_mulhi_epi16(delta,gain);
      const auto product=_mm_srai_epi32(_mm_unpacklo_epi16(lo,hi),11);
      value=_mm_max_epi16(zero,_mm_adds_epi16(_mm_packs_epi32(product,zero),input));
    }else throw std::runtime_error("unexpected reference bitmap primitive");
    d[i]=std::bit_cast<sU64>(_mm_cvtsi128_si64(value));
#else
    auto v=unpack(x[i]);
    if(mode==BI_GRAY)v=wz3::color_pixel(v,{},3);
    else if(mode==BI_INVERT)v=wz3::color_pixel(v,{},4);
    else if(mode==BI_RANGE)v=wz3::range_pixel(v,unpack(s[0]),unpack(s[1]),0);
    else {wz3::Texture a,b;a.width=b.width=a.height=b.height=1;a.pixels={unpack(d[i])};b.pixels={v};wz3::sharpen_bitmap(a,b,std::bit_cast<std::int16_t>(std::uint16_t(s[0]))/2048.f);v=a.pixels[0];}
    d[i]=pack(v);
#endif
  }
}
struct BilinearContext {wz3::Texture texture;unsigned flags;};
void BilinearSetup(BilinearContext* context,sU64* source,int w,int h,int border) {
  context->texture.width=w;context->texture.height=h;context->flags=border&3;
  for(int i=0;i<w*h;++i)context->texture.pixels.push_back(unpack(source[i]));
}
void BilinearFilter(BilinearContext* c,sU64* output,int u,int v){*output=pack(wz3::sample_bitmap(c->texture,u,v,c->flags));}
GenBitmap* Bitmap_Blur(GenBitmap* bm,int order,float sx,float sy,float amp) {
  auto t=bm->texture(),scratch=t;wz3::blur_bitmap(t,scratch,order,sx,sy,amp);for(int i=0;i<bm->Size;++i)bm->Data[i]=pack(t.pixels[i]);return bm;
}
#define __stdcall
#define NOTEXTURES1(a)
#define NOTEXTURES2(a,b)
#define SCRIPTVERIFY(x) check(bool(x),"reference bitmap precondition")
#include "bitmap_extra_oracle.inc"
#undef SCRIPTVERIFY
#undef NOTEXTURES2
#undef NOTEXTURES1
#undef __stdcall
std::vector<wz3::Pixel> render(unsigned command,const wz3::Texture& a,const wz3::Texture& b,std::span<const std::uint32_t> p) {
  GenBitmap first(a),second(b);const auto r=[&](unsigned i){return wz3::as_float(p[i]);};GenBitmap* output;
  if(command==0x35)output=Bitmap_Range(&first,p[0],p[1],p[2]);
  else if(command==0x3b)output=Bitmap_ColorBalance(&first,{r(0),r(1),r(2)},{r(3),r(4),r(5)},{r(6),r(7),r(8)});
  else if(command==0x2d)output=Bitmap_Distort(&first,&second,r(0),p[1]);
  else output=Bitmap_Sharpen(&first,p[0],r(1),r(2),r(3));
  auto pixels=output->texture().pixels;if(output!=&first && output!=&second)delete output;return pixels;
}
}
namespace {
template<class F> void rejects(F action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}check(rejected,"invalid bitmap filter accepted");}
std::vector<wz3::Pixel> compare(unsigned command,const wz3::Texture& source,const wz3::Texture& displacement,std::span<const std::uint32_t> p) {
  auto actual=command==0x2d ? displacement : source;
  if(command==0x35)for(auto& pixel:actual.pixels)pixel=wz3::range_pixel(pixel,wz3::color64(p[1]),wz3::color64(p[2]),p[0]);
  else if(command==0x3b)wz3::balance_bitmap(actual,p);
  else if(command==0x2d)wz3::distort_bitmap(source,displacement,actual,wz3::as_float(p[0]),p[1]);
  else {auto scratch=source;wz3::blur_bitmap(actual,scratch,p[0]|16,wz3::as_float(p[1]),wz3::as_float(p[2]),1);wz3::sharpen_bitmap(actual,source,wz3::as_float(p[3]));}
  const auto expected=original::render(command,source,displacement,p);
  check(actual.pixels==expected,("bitmap filter "+std::to_string(command)+" differs from archived handler").c_str());return expected;
}
void synthetic() {
  wz3::Texture source;source.width=16;source.height=8;source.pixels.resize(128);auto displacement=source;std::uint32_t random=217;
  const auto next=[&](){return random=random*1664525+1013904223;};
  for(auto& p:source.pixels)for(auto& c:p)c=next()>>16;
  for(auto& p:displacement.pixels)for(auto& c:p)c=next()>>16;
  unsigned cases=0;
  for(unsigned mode=0;mode<6;++mode)for(unsigned i=0;i<64;++i){const std::array<std::uint32_t,3> p={mode,next(),next()};compare(0x35,source,displacement,p);++cases;}
  for(unsigned border=0;border<8;++border)for(float amount:{-2.5f,-.13f,0.f,.01f,.5f,3.5f}){const std::array<std::uint32_t,2> p={f(amount),border};compare(0x2d,source,displacement,p);++cases;}
  auto reshaped=source;reshaped.width=8;reshaped.height=16;
  compare(0x2d,reshaped,displacement,std::array<std::uint32_t,2>{f(.3f),2});++cases;
  for(unsigned order:{1u,2u,3u})for(float radius:{0.f,.15f,.5f})for(float amplitude:{-32.f,-1.f,0.f,.5f,2.f,32.f}){const std::array<std::uint32_t,4> p={order,f(radius),f(radius/2),f(amplitude)};compare(0x38,source,displacement,p);++cases;}
  for(auto& p:source.pixels)for(unsigned c=0;c<3;++c)p[c]&=32767;
  for(unsigned i=0;i<100;++i) {
    std::array<std::uint32_t,9> p;for(auto& v:p)v=f(float(int(next()%4097)-2048)/1024.f);
    compare(0x3b,source,displacement,p);++cases;
  }
  // Color balance leaves alpha untouched, including the normal-map sentinel.
  auto identity=source;wz3::balance_bitmap(identity,std::array<std::uint32_t,9>{});
  for(std::size_t i=0;i<source.pixels.size();++i)check(identity.pixels[i][3]==source.pixels[i][3],"ColorBalance changed alpha");
  rejects([&]{wz3::balance_bitmap(identity,std::array<std::uint32_t,8>{});});identity.pixels[0][0]=65535;
  rejects([&]{wz3::balance_bitmap(identity,std::array<std::uint32_t,9>{});});
  rejects([&]{wz3::distort_bitmap(source,displacement,displacement,1,0);});rejects([&]{wz3::range_pixel({}, {}, {},6);});
  // Saved graph wiring also checks that Distort inherits its displacement
  // bitmap's format/mipmap fields rather than the sampled image's metadata.
  FrameFixture fixture;const auto a=fixture.add(0x34,{4,3,0xff102030,0xffaabbcc,f(.17f),f(.4f),f(1),0});
  const auto normal=fixture.add(0x2e,{f(.5f),1},{a}),b=fixture.add(0x25,{5,1,3},{normal});
  const auto image=wz3::evaluate_texture(fixture.p,fixture.g,a),map=wz3::evaluate_texture(fixture.p,fixture.g,b);
  const std::vector<std::pair<unsigned,std::vector<std::uint32_t>>> cases_by_opcode={{0x35,{3,0xff102030,0x80a0b0c0}},{0x3b,{f(.1f),0,0,0,f(-.3f),0,0,0,f(.2f)}},{0x2d,{f(.5f),5}},{0x38,{2,f(.2f),f(.3f),f(1.25f)}}};
  for(const auto& [cmd,p]:cases_by_opcode) {
    const auto id=fixture.add(cmd,p,cmd==0x2d ? std::vector<std::size_t>{a,b} : std::vector<std::size_t>{a});
    const auto actual=wz3::evaluate_texture(fixture.p,fixture.g,id);check(actual->pixels==compare(cmd,*image,*map,p),"bitmap evaluator input order/parameters");
    if(cmd==0x2d)check(actual->format==5 && actual->mip_count==1 && actual->mip_threshold==3,"Distort lost displacement metadata");
  }
  for(unsigned flags=0;flags<4;++flags) {
    const std::vector<std::uint32_t> p={f(.5f),f(.5f),f(.1f),f(.2f),f(.2f),f(.1f),0xff123456,f(.7f),f(1.3f),0,flags};
    const auto old=fixture.add(0x27,p,{a}),current=fixture.add(0x39,p,{a});
    check(wz3::evaluate_texture(fixture.p,fixture.g,old)->pixels==wz3::evaluate_texture(fixture.p,fixture.g,current)->pixels,"GlowRectOld handler alias");
  }
  const std::array<std::uint32_t,9> balance={f(.3f),f(-.2f),0,f(.1f),f(.7f),0,0,f(.8f),f(-.1f)};
  auto baseline=source;wz3::balance_bitmap(baseline,balance);
  const auto saved=std::fegetround();std::fesetround(FE_DOWNWARD);
  auto rounded=source;wz3::balance_bitmap(rounded,balance);
  check(std::fegetround()==FE_DOWNWARD,"ColorBalance lost caller rounding mode");std::fesetround(saved);
  check(rounded.pixels==baseline.pixels,"ColorBalance depends on caller rounding mode");
  std::cout<<cases<<" Range/ColorBalance/Distort/Sharpen cases matched archived handlers and SIMD primitives\n";
}
void debris(const char* path) {
  const auto project=wz3::read_project(path);const auto graph=wz3::connect(project);unsigned recipes=0;
  wz3::Texture source;source.width=32;source.height=16;source.pixels.resize(512);auto displacement=source;
  for(unsigned i=0;i<512;++i){source.pixels[i]={std::uint16_t(i*53%32768),std::uint16_t(i*83%32768),std::uint16_t(i*131%32768),std::uint16_t(i*197)};displacement.pixels[i]={16384,std::uint16_t(14500+i*7),std::uint16_t(12500+i*13),65535};}
  for(std::size_t id=0;id<project.operators.size();++id)if(graph.nodes[id].reachable) {
    const auto& op=project.operators[id];if(op.command==0x35 || op.command==0x3b || op.command==0x2d || op.command==0x38){compare(op.command,source,displacement,op.parameters);++recipes;}
  }
  std::cout<<recipes<<" saved Debris color/filter parameter sets matched with controlled inputs\n";
}
}
int main(int argc,char** argv){try{synthetic();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
