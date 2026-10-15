// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "text_layout.hpp"
#include <algorithm>
#include <cfenv>
#include <cstring>
#include <iostream>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace original {
using sInt=int;using sU32=std::uint32_t;using sU64=std::uint64_t;using sU16=std::uint16_t;using sS16=std::int16_t;using sU8=std::uint8_t;using sF32=float;using sChar=char;
struct letterz {sU16 xp,yp;sS16 w0,w1,w2;};
struct KOp {void* GetBlob(int& size){size=0;return nullptr;}void SetBlob(void*,int){}};struct KEnvironment{};
void sSetMem(void* d,int v,std::size_t n){std::memset(d,v,n);}
sU64 pack(wz3::Pixel p){std::swap(p[0],p[2]);return std::bit_cast<sU64>(p);}
wz3::Pixel unpack(sU64 v){auto p=std::bit_cast<wz3::Pixel>(v);std::swap(p[0],p[2]);return p;}
struct GenBitmap {
  int XSize,YSize,Size;std::vector<sU64> pixels;sU64* Data;
  explicit GenBitmap(const wz3::Texture& t):XSize(t.width),YSize(t.height),Size(t.pixels.size()) {for(auto p:t.pixels)pixels.push_back(pack(p));Data=pixels.data();}
};
bool CheckBitmap(GenBitmap* bm,GenBitmap** in){*in=bm;return false;}
void Fade64(sU64& d,sU64 a,sU64 b,int fade) {
#if defined(__SSE2__)
  auto f=_mm_srai_epi32(_mm_cvtsi32_si128(fade),1),g=_mm_sub_epi32(_mm_set1_epi32(32768),f);
  f=_mm_packs_epi32(f,f);g=_mm_packs_epi32(g,g);
  f=_mm_unpacklo_epi16(f,f);f=_mm_unpacklo_epi16(f,f);g=_mm_unpacklo_epi16(g,g);g=_mm_unpacklo_epi16(g,g);
  d=std::bit_cast<sU64>(_mm_cvtsi128_si64(_mm_slli_epi16(_mm_add_epi16(_mm_mulhi_epi16(_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(a)),g),
    _mm_mulhi_epi16(_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(b)),f)),1)));
#else
  d=pack(wz3::fade64(unpack(a),unpack(b),fade));
#endif
}
struct Print {int x,y;std::string text;bool operator==(const Print&)const=default;};
struct System {
  int width{},height{};std::vector<sU32> mask;std::vector<Print> prints;
  int FontBegin(int w,int h,const char*,int,int,int){width=w;height=h;mask.assign(w*h,0);prints.clear();return 37;}
  int FontWidth(const char* text,int count){int w=0;for(int i=0;i<count;++i)w+=5+static_cast<unsigned char>(text[i])%11;return w;}
  void FontPrint(int x,int y,const char* text,int count) {
    prints.push_back({x,y,std::string(text,count)});
    for(int i=0;i<count;++i) {
      const int w=FontWidth(text+i,1);
      for(int dy=0;dy<29;++dy)for(int dx=0;dx<w;++dx)if(x+dx>=0 && x+dx<width && y+dy>=0 && y+dy<height)
        mask[(y+dy)*width+x+dx]=(dx*17+dy*7+static_cast<unsigned char>(text[i]))&255;
      x+=w;
    }
  }
  sU32* FontBitmap(){return mask.data();}void FontEnd(){}
} system;
System* sSystem=&system;
#define sINTRO 1
#define sPLAYER 1
#define NOTEXTURES1(a)
#define __stdcall
#include "paragraph_oracle.inc"
#undef __stdcall
#undef NOTEXTURES1
#undef sPLAYER
#undef sINTRO
}
namespace {
void compare(const wz3::FontLayout& layout) {
  wz3::Texture source;source.width=32;source.height=16;source.pixels.resize(512);
  for(unsigned i=0;i<512;++i)source.pixels[i]={std::uint16_t(i*17),std::uint16_t(i*47),std::uint16_t(i*71),std::uint16_t(i*103)};
  original::GenBitmap bm(source);original::KOp op;original::KEnvironment env;
  auto text=layout.characters,font=layout.requested_family;
  original::Bitmap_Text(&op,&env,&bm,layout.x,layout.y,layout.height,layout.width,layout.color,layout.flags,
    layout.outer_spacing,layout.inner_spacing,layout.line_skip,text.data(),font.data());
  const auto expected_calls=original::system.prints;
  original::System native;native.FontBegin(128,64,"test",0,0,4);
  const auto lines=wz3::detail::paragraph_lines(layout,32,16,37,[&](auto s){return native.FontWidth(s.data(),s.size());});
  for(auto line:lines)native.FontPrint(line.x,line.y,line.text.data(),line.text.size());
  check(native.prints==expected_calls,"paragraph positions differ from archived Bitmap_Text");
  std::vector<std::uint8_t> mask;for(auto p:native.mask)mask.push_back(p&255);
  wz3::detail::composite_text_mask(source,mask,layout.color);
  for(unsigned i=0;i<512;++i)check(source.pixels[i]==original::unpack(bm.pixels[i]),"paragraph supersampling/compositing differs from archived Bitmap_Text");
}
void layout_cases() {
  wz3::FontLayout layout{.5f,.5f,.2f,0,0,2,0,0x95e3b791,"","Arial"};unsigned cases=0;
  for(unsigned flags=0;flags<4;++flags)for(float skip:{0.f,.7f,1.f,1.5f,-.5f})for(float x:{-.173f,.5f,1.1f})
    for(auto text:{"", "hund.", "a-z --", "first\n\nlast\n", "\n\n", "a\nbc", "\xB4\x80\xE9"}) {
      layout.flags=flags;layout.line_skip=skip;layout.x=x;layout.y=x;layout.characters=text;compare(layout);++cases;
    }
  std::cout<<cases<<" paragraph layouts and masks match archived Bitmap_Text: centering, clipping, blank/trailing lines and spacing\n";
}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}check(rejected,"invalid paragraph accepted");}
void real_font(const char* file) {
  wz3::Texture base;base.width=256;base.height=128;base.pixels.assign(32768,wz3::color64(0x70305070));
  base.fonts[2]=std::make_shared<wz3::FontAtlas>();base.format=5;base.mip_count=3;
  wz3::FontLayout l{.5f,.5f,.2f,0,0,2,0,0xffffffff,"hund.\n\xB4\x80\xE9","Arial",3,1.25f};
  auto output=base;wz3::draw_text_paragraph(output,l,file);
  check(output.fonts==base.fonts && output.format==5 && output.mip_count==3,"paragraph changed input metadata");
  const auto blank=wz3::fade64(base.pixels[0],wz3::color64(l.color),0);
  const auto ink=std::count_if(output.pixels.begin(),output.pixels.end(),[&](auto p){return p!=blank;});
  check(ink>100 && ink<output.pixels.size()/2,"paragraph has no useful coverage");
  auto stretched=base;l.width=.05f;wz3::draw_text_paragraph(stretched,l,file);check(stretched.pixels!=output.pixels,"explicit average width ignored");
  const auto saved=std::fegetround();std::fesetround(FE_DOWNWARD);auto rounded=base;wz3::draw_text_paragraph(rounded,l,file);
  check(std::fegetround()==FE_DOWNWARD,"paragraph lost caller rounding mode");std::fesetround(saved);check(rounded.pixels==stretched.pixels,"paragraph depends on caller rounding");
  rejects([&]{wz3::draw_text_paragraph(output,l,{});});l.height=0;rejects([&]{wz3::draw_text_paragraph(output,l,file);});
}
void debris(const char* path,const char* font) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);const wz3::TextureOptions options{font};unsigned count=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0x31) {
    const auto& op=p.operators[id];if(op.blob_size || (op.parameters[5]&0x70))continue;
    const auto& q=op.parameters;
    wz3::FontLayout layout{wz3::as_float(q[0]),wz3::as_float(q[1]),wz3::as_float(q[2]),wz3::as_float(q[3]),wz3::as_float(q[7]),int(q[6]),0,q[4],op.strings[0],op.strings[1],q[5],q.size()>8 ? wz3::as_float(q[8]) : 1.f};
    compare(layout);
    const auto source=wz3::evaluate_texture(p,g,g.nodes[id].inputs.at(0),options),actual=wz3::evaluate_texture(p,g,id,options);
    auto expected=*source;wz3::draw_text_paragraph(expected,layout,font);
    check(actual->pixels==expected.pixels,"saved paragraph evaluator parameters/metadata");++count;
  }
  std::cout<<count<<" reachable Debris paragraphs render with explicit substitute font "<<font<<"; GDI/Arial pixel equivalence is not established\n";
}
}
int main(int argc,char** argv){try{layout_cases();if(argc==3){real_font(argv[2]);debris(argv[1],argv[2]);}}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
