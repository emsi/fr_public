// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <cfenv>
#include <iostream>
#include <limits>
namespace source {
using sInt=int;using sU32=std::uint32_t;using sF32=float;
struct sVector {
  float x,y,z,w;
  void Init(float a,float b,float c,float d){x=a;y=b;z=c;w=d;}
  void InitColor(sU32 c){Init(((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f,(c>>24)/255.f);}
  void Scale4(float v){x*=v;y*=v;z*=v;w*=v;}
};
struct sMaterialInstance {int NumVSConstants,NumPSConstants,NumTextures; sVector *VSConstants,*PSConstants;int Textures[1];};
struct KOp {};struct KEnvironment {};
wz3::ColorCorrectionConstants constants;unsigned draws{};
struct Material {
  void SetInstance(const sMaterialInstance& i){check(i.NumVSConstants==4 && i.NumPSConstants==7 && i.NumTextures==1 && i.Textures[0]==123,"original Color Correction bindings");for(unsigned c=0;c<7;++c){const auto& v=i.PSConstants[c];constants[c]={v.x,v.y,v.z,v.w};}}
} material;
struct System {int CurrentViewport=1;void SetViewport(int v){check(v==1,"Color Correction changed viewport");}} system;
auto* sSystem=&system;
struct Targets {int Get(int id,float& u,float& v){check(id==0,"Color Correction capture ID");u=.5f;v=.25f;return 123;}} targets;
auto* RenderTargetManager=&targets;
struct Overlay {Material* ColorCorrectMaterial=&material;void FXQuad(Material* m,const float* uv){check(m==&material && uv[0]==.5f && uv[1]==.25f,"Color Correction draw or UVs");++draws;}} overlay;
auto* GenOverlayManager=&overlay;
#define __stdcall
#include "color_correction_oracle.inc"
#undef __stdcall
wz3::ColorCorrectionConstants record(std::span<const std::uint32_t> p){draws=0;Exec_Effect_ColorCorrection(nullptr,nullptr,p[0],wz3::as_float(p[1]),wz3::as_float(p[2]),p[3],p[4],p[5],p[6],p[7],p[8],p[9],wz3::as_float(p[10]));check(draws==1,"Color Correction draw count");return constants;}
}
namespace {
std::vector<std::uint32_t> parameters(){return {30,f(.1f),f(.8f),0x6789abcd,0xe1c3a587,0xabcdef01,0x31415926,0,0xfedcba98,0x02468ace,f(1.7f)};}
void compare(std::span<const std::uint32_t> p){check(wz3::color_correction_constants(p)==source::record(p),"Color Correction native constants differ from original handler");}
void cases(){
  unsigned count=0;
  for(unsigned flags:{0u,1u,2u,3u})for(float amp:{-1.f,0.f,.5f,1.f,2.75f})for(float a:{-.25f,0.f,.125f,.9f})for(float b:{-.5f,.25f,1.f,1.5f}){auto p=parameters();p[1]=f(a);p[2]=f(b);p[7]=flags;p[10]=f(amp);compare(p);++count;}
  auto p=parameters();const auto expected=wz3::color_correction_constants(p);const auto old=std::fegetround();std::fesetround(FE_DOWNWARD);const auto actual=wz3::color_correction_constants(p);check(std::fegetround()==FE_DOWNWARD,"Color Correction lost caller rounding");std::fesetround(old);check(actual==expected,"Color Correction depends on caller rounding");
  for(unsigned i=0;i<4;++i){auto p=parameters();if(i==0)p.pop_back();else if(i==1)p[2]=p[1];else if(i==2)p[10]=f(std::numeric_limits<float>::infinity());else p[1]=f(std::numeric_limits<float>::quiet_NaN());bool caught=false;try{wz3::color_correction_constants(p);}catch(const std::runtime_error&){caught=true;}check(caught,"invalid Color Correction accepted");}
  std::cout<<count<<" Color Correction constant sets match archived handler, including debug mode and reversed thresholds\n";
}
void scene(){FrameFixture x;auto p=parameters();p[0]=4;const auto fx=x.add(0x75,p),adjust=x.add(0x180,{2},{fx}),view=x.viewport();x.g.nodes[view].inputs={adjust};x.animate(fx,0,10);wz3::ScenePlayer player(x.p,x.g);auto env=wz3::frame_environment(65536);env.variables[3].fill(17);const auto s=player.viewport(view,env,2);check(s.effects.size()==1 && s.effects[0].color_correction.has_value(),"missing Color Correction scene effect");check(s.effects[0].sort_key==(6u<<24),"Color Correction must use initial render pass plus adjustment");check(s.effects[0].color_correction->parameters[10]==f(1),"Color Correction execution animation missing");}
void debris(const char* path){const auto p=wz3::read_project(path);const auto g=wz3::connect(p);const wz3::SplineLibrary curves(p);unsigned count=0;for(std::size_t i=0;i<p.operators.size();++i)if(g.nodes[i].reachable && p.operators[i].command==0x75){const auto& op=p.operators[i];for(float time:{0.f,.125f,.5f,.875f,1.f}){auto env=wz3::frame_environment(int(time*65536));const wz3::AnimationScope animation(wz3::compile_animation(op,curves),curves,op.parameters,env);compare(animation.parameters);}++count;}check(count==12,"Debris Color Correction recipe count changed");std::cout<<count<<" saved Color Correction recipes match at five animation times\n";}
}
int main(int argc,char** argv){try{cases();scene();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
