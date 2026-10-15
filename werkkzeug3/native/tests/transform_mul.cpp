// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <iostream>
namespace source {
using sInt=int;using sU32=std::uint32_t;using sF32=float;constexpr int KC_BITMAP=1;
int allocated{},skipped{};
struct GenBitmap {
  int ClassId=KC_BITMAP,refs=1;wz3::Texture texture;
  explicit GenBitmap(const wz3::Texture& t):texture(t){++allocated;}
  ~GenBitmap(){--allocated;}
  void AddRef(){++refs;}void Release(){if(--refs==0)delete this;}
  GenBitmap* Copy(){return new GenBitmap(texture);}
};
GenBitmap* writable(GenBitmap* p){if(p->refs==1)return p;auto* copy=p->Copy();p->Release();return copy;}
GenBitmap* Bitmap_Color(GenBitmap* p,int mode,unsigned color){p=writable(p);for(auto& v:p->texture.pixels)v=wz3::color_pixel(v,wz3::color64(color),mode);return p;}
GenBitmap* Bitmap_Rotate(GenBitmap* p,float angle,float sx,float sy,float tx,float ty,int border,int,int) {
  // SCRIPTVERIFY precedes CheckBitmap: a failed copy leaves its input reference
  // for RotateMul to release, then Bitmap_Merge accepts a null second input.
  if(sx==0 || sy==0){++skipped;return nullptr;}
  const auto input=p->texture;p=writable(p);wz3::transform_bitmap(input,p->texture,angle,sx,sy,tx,ty,border);return p;
}
GenBitmap* Bitmap_Merge(int mode,int count,GenBitmap* a,GenBitmap* b){check(a && count==2,"invalid reference merge");a=writable(a);if(b){for(std::size_t i=0;i<a->texture.pixels.size();++i)a->texture.pixels[i]=wz3::merge_pixel(a->texture.pixels[i],b->texture.pixels[i],mode);b->Release();}return a;}
double sFPow(double a,double b){return std::pow(a,b);}
#define __stdcall
#define NOTEXTURES1(x)
#define SCRIPTVERIFY(x) if(!(x))return nullptr
#include "transform_mul_oracle.inc"
#undef SCRIPTVERIFY
#undef NOTEXTURES1
#undef __stdcall
wz3::Texture run(const wz3::Texture& input,const std::vector<std::uint32_t>& p) {
  check(allocated==0,"reference leaked input bitmaps");skipped=0;
  auto* result=Bitmap_RotateMul(new GenBitmap(input),wz3::as_float(p[0]),wz3::as_float(p[1]),wz3::as_float(p[2]),wz3::as_float(p[3]),wz3::as_float(p[4]),p[5],p[6],p[7],p[8],p[9]);
  check(result,"reference TransformMul failed");auto output=result->texture;result->Release();check(allocated==0,"reference TransformMul leaked a skipped copy");return output;
}
}
namespace {
void compare(const std::vector<std::uint32_t>& p) {
  FrameFixture x;const auto input=x.add(0x34,{4,3,0x12b02e19,0xf8e0c985,f(.4f),f(.12f),f(.7f),0});
  const auto transform=x.add(0x36,p,{input});const auto base=wz3::evaluate_texture(x.p,x.g,input);const auto expected=source::run(*base,p);const auto actual=wz3::evaluate_texture(x.p,x.g,transform);
  check(actual->pixels==expected.pixels,"TransformMul differs from archived iteration/fade/merge sequence");
}
void cases() {
  unsigned count=0,skips=0;
  for(float sx:{0.f,-1.f,.25f,1.f,2.f})for(float sy:{0.f,-1.f,1.f})for(unsigned copies:{1u,2u,3u,5u})for(unsigned mode:{0u,1u,2u,6u,8u,9u,10u,16u,17u,18u})for(unsigned fade:{0xffffffff,0xc0d0e0f0}) {
    if((mode&16) && (sx==0 || sy==0))continue;
    std::vector<std::uint32_t> p={f(.17f),f(sx),f(sy),f(.27f),f(.62f),0,0xffb0d0f0,mode,copies,fade};compare(p);skips+=source::skipped;++count;
  }
  check(skips>0,"TransformMul regression missed null copies");std::cout<<count<<" TransformMul cases match archived sequence, including "<<skips<<" skipped zero-scale copies\n";
}
void debris(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);unsigned count=0;
  for(std::size_t i=0;i<p.operators.size();++i)if(g.nodes[i].reachable && p.operators[i].command==0x36){compare(p.operators[i].parameters);++count;}
  const auto& op=p.operators[34593];check(op.command==0x36 && op.parameters[1]==0,"Debris zero-scale fixture changed");
  const auto input=wz3::evaluate_texture(p,g,g.nodes[34593].inputs.at(0));const auto expected=source::run(*input,op.parameters);const auto actual=wz3::evaluate_texture(p,g,34593);
  check(actual->pixels==expected.pixels && source::skipped==1,"Debris zero-scale copy regression");
  for(auto id:{34584u,34585u,34603u})check(!wz3::evaluate_texture(p,g,id)->pixels.empty(),"Debris material texture graph did not finish");
  std::cout<<count<<" reachable Debris TransformMul parameter sets match; material 34594's three saved texture graphs evaluate\n";
}
}
int main(int argc,char** argv){try{cases();if(argc==2)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
