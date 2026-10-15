// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/glare.hpp"
#include <algorithm>
#include <cfenv>
#include <iostream>
#include <limits>
namespace source {
using sInt=int;using sU32=std::uint32_t;using sF32=float;using sBool=bool;
struct sVector {
  float x,y,z,w;
  void Init(float a,float b,float c,float d){x=a;y=b;z=c;w=d;}
  void InitColor(sU32 c){Init(((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f,(c>>24)/255.f);}
  std::array<float,4> array() const{return {x,y,z,w};}
};
struct Rect {int x0{},y0{},x1{},y1{};};struct sViewport {Rect Window;};
struct Transform {void Init(){}};
struct sMaterialEnv {int Orthogonal;Transform CameraSpace,ModelSpace;void Init(){}};
constexpr int sMEO_PIXELS=1,GENOVER_TEX1=0;
struct sMaterialInstance {int NumVSConstants,NumPSConstants,NumTextures; sVector* VSConstants; sVector* PSConstants;int Textures[5];};
struct Material {
  wz3::GlarePass::Mode mode;const sMaterialInstance* instance{};int texture{};std::array<unsigned,1> Color;
  void SetInstance(const sMaterialInstance& i){instance=&i;}void SetTex(int,int t){texture=t;}void Set(const sMaterialEnv&){}
};
struct KOp {};struct KEnvironment {};
std::vector<wz3::GlarePass> passes;unsigned captures{},clears{};
struct System {
  sViewport CurrentViewport;std::array<unsigned,4> widths{},heights{};
  void GetTextureSize(int t,int& x,int& y){x=widths[t];y=heights[t];}
  void ClearTarget(int id,sU32 color){check(id==0 && color==0xff000000,"Glare original clear");++clears;}
  void SetViewport(const sViewport& v){CurrentViewport=v;}
} system;
auto* sSystem=&system;
struct Targets {
  unsigned width{},height{};
  static unsigned index(unsigned id){switch(id){case 0:return 0;case 0x10000:return 3;case 0x20010:return 1;case 0x20011:return 2;default:throw std::runtime_error("unknown original Glare target");}}
  int Get(unsigned id,float& u,float& v){const auto i=index(id);u=i==0 ? float(width)/system.widths[0] : 1;v=i==0 ? float(height)/system.heights[0] : 1;return i;}
  void GrabToTarget(unsigned id,const Rect* window) {
    if(id==0){++captures;return;}
    check(!passes.empty(),"Glare grab without draw");auto& p=passes.back();p.target=index(id);
    check(p.width==unsigned(window->x1-window->x0) && p.height==unsigned(window->y1-window->y0),"Glare capture rectangle");
  }
} targets;
auto* RenderTargetManager=&targets;
struct Overlay {
  Material filter{wz3::GlarePass::filter},tone{wz3::GlarePass::tone},combine{wz3::GlarePass::combine},copy{wz3::GlarePass::copy};
  Material *GlareMaterial=&filter,*Glare2Material=&tone,*Glare3Material=&combine;
  std::array<Material*,1> Mtrl{&copy};
  void FXQuad(Material* m,float* uv,int=0,float* uv1=nullptr,int=0) {
    wz3::GlarePass p;p.mode=m->mode;p.target=3;p.width=system.CurrentViewport.Window.x1;p.height=system.CurrentViewport.Window.y1;
    p.uv={uv[0],uv[1]};p.original_uv={float(targets.width)/system.widths[0],float(targets.height)/system.heights[0]};
    if(m->mode==wz3::GlarePass::copy)p.source=m->texture;
    else {
      const auto& i=*m->instance;p.source=i.Textures[0];
      if(m->mode==wz3::GlarePass::filter) {
        p.constants[0]=i.PSConstants[0].array();p.constants[0][3]=i.PSConstants[0].w;
        for(unsigned n=0;n<9;++n) {
          const auto v=i.VSConstants[4+n/2].array();const auto c=i.PSConstants[(n+3)/4].array();
          p.taps[n]={v[(n%2)*2],v[(n%2)*2+1],c[(n+3)%4]};
        }
      }else for(int n=0;n<i.NumPSConstants;++n)p.constants[n]=i.PSConstants[n].array();
      if(m->mode==wz3::GlarePass::combine) {check(i.Textures[1]==0 && uv1,"Glare combine original source");p.original_uv={uv1[0],uv1[1]};}
    }
    passes.push_back(p);
  }
  void FXQuad(int id,float* uv){FXQuad(Mtrl[id],uv);}
} overlay;
auto* GenOverlayManager=&overlay;
#define __stdcall
#include "glare_oracle.inc"
#undef __stdcall
std::vector<wz3::GlarePass> record(std::span<const std::uint32_t> p,unsigned w,unsigned h,unsigned cw,unsigned ch) {
  system.CurrentViewport={{0,0,int(w),int(h)}};system.widths={cw,512,256,cw};system.heights={ch,256,128,ch};targets.width=w;targets.height=h;passes.clear();captures=clears=0;
  Exec_Effect_Glare(nullptr,nullptr,p[0],p[1],p[2],p[3],p[4],wz3::as_float(p[5]),wz3::as_float(p[6]),wz3::as_float(p[7]),wz3::as_float(p[8]),wz3::as_float(p[9]),wz3::as_float(p[10]));
  check(captures==1 && clears==1,"Glare must clear and capture original once");return passes;
}
}
namespace {
std::vector<std::uint32_t> parameters(){return {31,0,0xff745149,0x81cfbba8,0xdfeddace,f(.0073f),f(.0021f),f(1.3f),f(.7f),f(.35f),f(.4f)};}
void compare(std::span<const std::uint32_t> p,unsigned w,unsigned h,unsigned cw,unsigned ch) {
  const auto expected=source::record(p,w,h,cw,ch),actual=wz3::glare_passes(p,w,h,cw,ch);check(actual.size()==expected.size(),"Glare pass count");
  for(unsigned i=0;i<actual.size();++i) {
    const auto& a=actual[i];const auto& e=expected[i];check(a.mode==e.mode && a.source==e.source && a.target==e.target && a.width==e.width && a.height==e.height,"Glare pass order/size");
    check(a.uv==e.uv && a.original_uv==e.original_uv,"Glare captured UV bounds");
    if(a.mode==wz3::GlarePass::copy)continue;
    if(a.mode==wz3::GlarePass::filter) {check(a.taps==e.taps,"Glare filter taps differ from original");check(a.constants[0]==e.constants[0],"Glare filter seed differs from original");}
    else check(a.constants==e.constants,"Glare tone/merge constants differ from original");
  }
}
void cases() {
  unsigned count=0;
  for(unsigned flags=0;flags<8;++flags)for(unsigned blur=0;blur<4;++blur)for(float gray:{-2.f,0.f,.5f,1.f,3.f})for(auto dims:{std::array{1280u,640u,1280u,640u},std::array{640u,360u,1920u,1080u},std::array{1920u,1080u,1920u,1080u}}) {
    auto p=parameters();p[1]=flags;p[5]=f(blur&1 ? .0073f : 0);p[6]=f(blur&2 ? .0021f : 0);p[9]=f(gray);
    compare(p,dims[0],dims[1],dims[2],dims[3]);++count;
  }
  auto p=parameters();const auto baseline=wz3::glare_passes(p,640,360,640,360);const auto old=std::fegetround();std::fesetround(FE_DOWNWARD);
  const auto rounded=wz3::glare_passes(p,640,360,640,360);check(std::fegetround()==FE_DOWNWARD,"Glare lost caller rounding");std::fesetround(old);
  check(baseline[0].taps==rounded[0].taps && baseline[1].constants==rounded[1].constants,"Glare depends on caller rounding");
  for(unsigned which=0;which<3;++which) {
    p=parameters();if(which==0)p.pop_back();else if(which==1)p[2]=0xffffffff;else p[5]=f(std::numeric_limits<float>::infinity());
    bool caught=false;try{wz3::glare_passes(p,640,360,640,360);}catch(const std::runtime_error&){caught=true;}check(caught,"invalid Glare parameters accepted");
  }
  std::cout<<count<<" Glare pass sequences match the archived handler\n";
}
void scene() {
  FrameFixture x;auto p=parameters();p[0]=4;const auto effect=x.add(0x74,p),adjust=x.add(0x180,{2},{effect}),viewport=x.viewport();x.g.nodes[viewport].inputs={adjust};
  x.animate(effect,0,5);wz3::ScenePlayer player(x.p,x.g);auto env=wz3::frame_environment(65536);const auto scene=player.viewport(viewport,env,2);
  check(scene.effects.size()==1 && scene.effects[0].glare.has_value(),"Glare not deferred into scene");
  check(scene.effects[0].sort_key==(6u<<24),"Glare pass adjustment/base usage");check(scene.effects[0].glare->parameters[5]==f(1),"Glare execution animation missing");
}
void debris(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);unsigned count=0;
  for(std::size_t i=0;i<p.operators.size();++i)if(g.nodes[i].reachable && p.operators[i].command==0x74){compare(p.operators[i].parameters,1280,640,1280,640);++count;}
  check(count>0,"missing real Glare recipes");std::cout<<count<<" reachable Debris Glare recipes match original pass setup\n";
}
}
int main(int argc,char** argv){try{cases();scene();if(argc==2)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
