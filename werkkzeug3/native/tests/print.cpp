// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/print.hpp"
#include <iostream>
#include <algorithm>
namespace source {
using sInt=int;using sF32=float;using sU16=std::uint16_t;using sU32=std::uint32_t;using sChar=char;using sBool=bool;
struct KLetterMetric {struct Rect {float x0,y0,x1,y1;float XSize(){return x1-x0;}float YSize(){return y1-y0;}} UV;float PreSpace,Width;};
struct Matrix {struct V{float x{},y{},z{};} l;};
struct sMaterialEnv {Matrix ModelSpace;int Orthogonal{};void Init(){*this={};}};
struct Shader {void Set(sMaterialEnv&) {}} shader;
struct GenMaterial {struct Pass {Shader* Mtrl=&shader;};struct List {int Count=1;Pass pass;Pass& operator[](int){return pass;}} Passes;};
struct KOp {GenMaterial* material;void* GetLinkCache(int){return material;}};
struct Game {struct {int Armor{},Life{},Ammo[4]{},CurrentWeapon{};} Player;int Switches[256]{};} game;
struct KEnvironment {KLetterMetric Letters[8][256];source::Game* Game=&game;sMaterialEnv CurrentCam;struct {Matrix m;Matrix& Top(){return m;}} ExecStack;};
constexpr int sMEO_NORMALISED=1,sFVF_TSPACE3=1,sGEO_TRI=1;
struct System {
  std::vector<float> vertices;std::vector<sU16> indices;
  int GeoAdd(int,int){return 1;}void SetViewProject(sMaterialEnv*){}
  void GeoBegin(int,int v,int i,float** vp,void** ip){vertices.resize(v*8);indices.resize(i);*vp=vertices.data();*ip=indices.data();}
  void GeoEnd(int,int v,int i){vertices.resize(v*8);indices.resize(i);}void GeoDraw(int){}void GeoRem(int){}
} system;
System* sSystem=&system;
float sFSin(float v){return std::sin(double(v));}float sFCos(float v){return std::cos(double(v));}
void SetVert(float* p,float x,float y,float z,float u,float v){p[0]=x;p[1]=y;p[2]=z;p[6]=u;p[7]=v;}
#define sQuad(ip,a,b,c,d) {ip[0]=a;ip[1]=b;ip[2]=c;ip[3]=a;ip[4]=c;ip[5]=d;ip+=6;}
#define __stdcall
#define sPLAYER 0
#include "print_oracle.inc"
#undef sPLAYER
#undef __stdcall
#undef sQuad
void compare(std::span<const std::uint32_t> p,std::string text,const std::array<wz3::GlyphMetric,256>& glyphs) {
  KEnvironment env{};const auto page=(p[0]>>4)&7;
  for(unsigned c=0;c<256;++c){const auto& m=glyphs[c];env.Letters[page][c]={{m.uv[0],m.uv[1],m.uv[2],m.uv[3]},m.pre_space,m.advance};}
  GenMaterial mat;KOp op{&mat};Exec_Effect_Print(&op,&env,p[0],wz3::as_float(p[1]),wz3::as_float(p[2]),wz3::as_float(p[3]),wz3::as_float(p[4]),wz3::as_float(p[5]),text.data());
  const auto actual=wz3::layout_print(p,text,glyphs);check(actual.vertices.size()*8==system.vertices.size(),"source/native Print glyph count");
  constexpr unsigned order[4]={0,1,3,2};
  for(std::size_t i=0;i<actual.vertices.size();++i) {
    const auto& v=actual.vertices[i];const auto* expected=system.vertices.data()+((i/4)*4+order[i%4])*8;
    for(unsigned c=0;c<3;++c)near(v.position[c],expected[c],"source/native Print position");
    near(v.uv[0],expected[6],"source/native Print U");near(v.uv[1],expected[7],"source/native Print V");check(v.color==0xffffffff,"Print default vertex color");
  }
}
}
namespace {
std::array<wz3::GlyphMetric,256> metrics() {
  std::array<wz3::GlyphMetric,256> glyphs{};
  for(unsigned c=32;c<128;++c)glyphs[c]={{.1f,.2f,.1f+.01f*(c%7+1),.3f},(c%3==0 ? -.005f : .01f),.01f*(c%7+2),true};return glyphs;
}
void layout() {
  const auto glyphs=metrics();unsigned count=0;
  for(unsigned alignment=0;alignment<4;++alignment)for(unsigned flags:{0u,2u,4u,6u})for(float radius:{0.f,3.f,-3.f})for(float size:{.4f,1.f,-.5f}) {
    const std::array<std::uint32_t,6> p={(alignment<<8)|16|flags,f(size),f(.3f),f(.02f),f(.04f),f(radius)};
    for(const char* text: {"Alpha Beta", "%|origin\n\nright %|edge\n", "literal %x, punctuation!?\n\n", "", "\n\nA"}) {source::compare(p,text,glyphs);++count;}
  }
  auto p=std::vector<std::uint32_t>{0x11,f(1),f(1),0,0};const auto legacy=wz3::layout_print(p,"ABC",glyphs);p[0]=0x110;p.push_back(0);const auto modern=wz3::layout_print(p,"ABC",glyphs);
  check(legacy.vertices[0].position==modern.vertices[0].position,"five-word center-X migration");
  auto released=p;released[0]=0x11;
  const auto centered=wz3::layout_print(released,"ABC",glyphs,true);
  check(centered.vertices[0].position==modern.vertices[0].position,"six-word release center-X migration");
  check(wz3::layout_print(released,"ABC",glyphs).vertices[0].position!=centered.vertices[0].position,
        "later six-word Print changed its alignment semantics");
  released[0]=0x211;
  check(wz3::layout_print(released,"ABC",glyphs,true).vertices[0].position==wz3::layout_print(released,"ABC",glyphs).vertices[0].position,
        "release compatibility overrode explicit alignment bits");
  for(const auto& text:std::vector<std::string>{"%3s0", "%s0a|b;", std::string(512,'A')}) {bool caught=false;try{wz3::layout_print(p,text,glyphs);}catch(const std::runtime_error&){caught=true;}check(caught,"unsupported Print formatting/line overflow accepted");}
  std::string many;for(unsigned i=0;i<20;++i)many+=std::string(300,'A')+'\n';source::compare(p,many,glyphs);check(wz3::layout_print(p,many,glyphs).vertices.size()==4096*4,"Print source geometry cap");
  std::cout<<count<<" Print layouts matched archived source (alignment, markers, blanks, spacing and curvature)\n";
}
void theta(const char* project_path,const char* font_path) {
  const auto p=wz3::read_project(project_path);const auto g=wz3::connect(p);const wz3::TextureOptions options{font_path};
  const auto texture=wz3::evaluate_texture(p,g,g.names.at("font_bitmap"),options);check(!texture->fonts[1]->glyphs['&'].present && texture->fonts[1]->glyphs['&'].uv[3]>0,"absent glyph metrics must retain source line height");
  auto migrated=p.operators[1774].parameters;migrated[0]=(migrated[0]&~1u)|0x100;migrated.push_back(0);
  source::compare(migrated,p.operators[1774].strings[0],texture->fonts[1]->glyphs);
  wz3::FramePlayer player(p,g,options);std::shared_ptr<const wz3::Texture> cached;std::vector<wz3::SceneEffectVertex> first;
  for(int beat:{514,552,590}) {
    const auto frame=player.frame(g.names.at("root"),beat*65536,320,240);bool found=false;
    for(const auto& cmd:frame.commands)if(const auto* scene=std::get_if<wz3::SceneFrame>(&cmd.draw))for(const auto& e:scene->effects)if(e.operator_id==1774) {
      check(e.vertices.size()==215*4 && e.textures.size()==1,"Theta credits glyph/texture count");check(!e.normalized,"Theta credits use their saved camera");
      if(cached)check(cached==e.textures.begin()->second,"Print atlas must persist across frames");cached=e.textures.begin()->second;
      if(first.empty())first=e.vertices;else check(first[0].position!=e.vertices[0].position,"credits ancestor animation must move text");found=true;
    }
    check(found,"credits Print draw absent from root");
  }
  wz3::FramePlayer missing_font(p,g);bool failed=false;try{missing_font.frame(g.names.at("root"),552*65536,32,32);}catch(const std::runtime_error& e){failed=std::string_view(e.what()).find("font-file")!=std::string_view::npos;}check(failed,"missing font must not silently substitute host typography");
  // Full CPU timeline traversal retains gait, particles and render ordering.
  wz3::FramePlayer continuous(p,g,options);unsigned frames=0;
  for(int ms=0;ms<=260000;ms+=100) {continuous.frame(g.names.at("root"),std::int32_t(double(ms)*wz3::as_float(p.settings[4])*65536/60000),64,48,ms);++frames;}
  std::cout<<frames<<" consecutive CPU timeline frames executed, including 215-glyph credits\n";
}
}
int main(int argc,char** argv) {
  try {layout();if(argc==3)theta(argv[1],argv[2]);}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
