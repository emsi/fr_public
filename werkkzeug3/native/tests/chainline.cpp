// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/chainline.hpp"
#include "spline_kernel.hpp"
#include <cfenv>
#include <iostream>
#include <limits>

namespace source {
using namespace wz3::legacy;
using sU16=std::uint16_t;
struct KInstanceMem {bool Reset=true;virtual ~KInstanceMem()=default;};
struct Camera {sMatrix ModelSpace,CameraSpace;};
struct Shader {void Set(Camera&) {}} shader;
struct GenMaterial {struct Pass {Shader* Mtrl=&shader;};struct List {int Count=1;Pass pass;Pass& operator[](int){return pass;}} Passes;};
struct KOp {GenMaterial* material;void* GetLinkCache(int){return material;}};
struct KEnvironment {
  std::array<sMatrix,32> Markers;int TimeSlices{};Camera CurrentCam;
  std::unique_ptr<KInstanceMem> memory;
  template<class T> T* GetInst(KOp*) {if(!memory)memory=std::make_unique<T>();else memory->Reset=false;return static_cast<T*>(memory.get());}
};
template<class T> void sSwap(T& a,T& b) {std::swap(a,b);}
float sFSqrt(float v) {return std::sqrt(double(v));}
constexpr unsigned sGT_MODELVIEW=1,sFVF_TSPACE3=1,sGEO_TRI=1;
void sQuad(sU16*& p,int a,int b,int c,int d) {for(int i:{a,b,c,a,c,d})*p++=sU16(i);}
struct System {
  sMatrix view;std::vector<float> vertices;std::vector<sU16> indices;
  int GeoAdd(int,int){return 1;}
  void GetTransform(int,sMatrix& m){m=view;}
  void GeoBegin(int,int nv,int ni,float** fp,void** ip){vertices.resize(nv*8);indices.resize(ni);*fp=vertices.data();*ip=indices.data();}
  void GeoEnd(int){}void GeoDraw(int){}void GeoRem(int){}
} system;
System* sSystem=&system;
#define sVERIFY(x) check(x,"archived ChainLine assertion")
#define sCOUNTOF(x) int((x).size())
#define sPLAYER 1
#include "chainline_oracle.inc"
#undef sPLAYER
#undef sCOUNTOF
#undef sVERIFY
sMatrix matrix(const wz3::SceneMatrix& m) {
  sMatrix out;out.i.Init(m[0],m[1],m[2],m[3]);out.j.Init(m[4],m[5],m[6],m[7]);out.k.Init(m[8],m[9],m[10],m[11]);out.l.Init(m[12],m[13],m[14],m[15]);return out;
}
void compare(wz3::ChainLines& native,KEnvironment& env,const std::vector<std::uint32_t>& p,const wz3::SceneMarkers& markers,wz3::FrameClock clock,const wz3::SceneMatrix& view,bool material=true) {
  for(unsigned i=0;i<32;++i)env.Markers[i]=matrix(markers[i]);
  env.TimeSlices=clock.reset ? 0 : clock.current/10-(clock.current-clock.delta)/10;
  system.view=matrix(view);system.vertices.clear();system.indices.clear();GenMaterial m;KOp op{material ? &m : nullptr};
  const auto real=[&](unsigned i){return wz3::as_float(p[i]);};
  Exec_Effect_ChainLine(&op,&env,{real(0),real(1),real(2),0},{real(3),real(4),real(5),0},p[6],p[7],real(8),real(9),real(10),real(11),real(12),real(13),real(14),real(15),p[16],{real(17),real(18),real(19),0},p[20]);
  const auto line=native.update(0,p,markers,clock);const auto& state=*static_cast<CLData*>(env.memory.get());
  check(line.ripped==state.Ripped,"ChainLine tear differs from archived handler");
  for(unsigned i=0;i<32;++i)for(unsigned c=0;c<3;++c)near(line.points[i][c],state.Lines[i].Pos[c],"ChainLine trajectory differs from archived handler");
  if(material) {
    const auto quads=wz3::chainline_vertices(line,view);check(quads.size()==124 && system.indices.size()==186,"ChainLine ribbon count");
    std::size_t index=0;
    for(unsigned base=0;base<124;base+=4)for(unsigned corner:{0u,1u,2u,0u,2u,3u}) {
      const auto& v=quads[base+corner];const auto* expected=system.vertices.data()+system.indices[index++]*8;
      for(unsigned c=0;c<3;++c)near(v.position[c],expected[c],"ChainLine ribbon position/index differs from archived handler");
      check(v.uv[0]==expected[6] && v.uv[1]==expected[7] && v.color==std::bit_cast<std::uint32_t>(expected[5]),"ChainLine ribbon UV/color differs from archived handler");
    }
  }else check(system.vertices.empty(),"materialless ChainLine drew geometry");
}
}
namespace {
std::vector<std::uint32_t> parameters(unsigned flags=0) {
  return {0,f(4),0,f(4),f(5),f(1),0,1,f(7),f(.2f),f(.002f),f(.99f),f(.03f),f(7),f(.001f),f(.3f),flags,f(.1f),f(.02f),f(-.05f),9};
}
wz3::SceneMarkers identity_markers() {wz3::SceneMarkers m;m.fill(wz3::scene_identity());return m;}
void oracle() {
  unsigned cases=0;
  for(unsigned flags=0;flags<16;++flags) {
    wz3::ChainLines native;source::KEnvironment env;auto p=parameters(flags);auto markers=identity_markers();wz3::FrameClock clock;
    for(int t:{0,1,9,10,16,19,20,35,50,100,260,527,600,1300,590,610,610,1400}) {
      markers[1]=wz3::scene_srt(std::array<std::uint32_t,9>{f(1),f(1),f(1),0,0,f(t*.00002f),f(t*.004f),0,0});
      const auto view=wz3::scene_srt(std::array<std::uint32_t,9>{f(2),f(3),f(.5f),f(.12f),f(.23f),f(.17f),0,0,f(5)});
      clock.advance(t);source::compare(native,env,p,markers,clock,view,t>=19);++cases;
    }
  }
  std::cout<<cases<<" archived ChainLine simulation/ribbon cases match\n";
}
void boundaries() {
  auto markers=identity_markers();auto p=parameters(8);wz3::ChainLines lines;wz3::FrameClock clock;
  const auto initial=lines.update(1,p,markers,clock);near(initial.points.front()[1],4,"ChainLine first endpoint");near(initial.points.back()[1],5,"ChainLine second endpoint");
  markers[0][12]=2;markers[1][13]=3;clock.advance(9);const auto moved=lines.update(1,p,markers,clock);
  near(moved.points.front()[0],2,"static ChainLine must follow markers without a time slice");near(moved.points.back()[1],8,"ChainLine second marker transform");
  p=parameters();wz3::ChainLines stepping;clock={};const auto a=stepping.update(0,p,identity_markers(),clock);
  clock.advance(9);check(stepping.update(0,p,identity_markers(),clock).points==a.points,"ChainLine advanced before a 10 ms boundary");
  clock.advance(10);const auto b=stepping.update(0,p,identity_markers(),clock);check(b.points!=a.points,"ChainLine lost a sub-10 ms interval crossing a time slice");
  clock.advance(0);check(stepping.update(0,p,identity_markers(),clock).points==b.points,"backward time reset ChainLine memory");
  check(stepping.update(1,p,identity_markers(),clock).points==a.points,"new ChainLine instance inherited existing state");
  const auto rejects=[&](auto action){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid ChainLine accepted");};
  p[6]=32;rejects([&]{lines.update(0,p,markers,clock);});p[6]=0;p[20]=32;rejects([&]{lines.update(0,p,markers,clock);});
  p[20]=9;p[8]=f(std::numeric_limits<float>::infinity());rejects([&]{lines.update(0,p,markers,clock);});
  p=parameters();clock={1000010,1000010,false};rejects([&]{lines.update(0,p,markers,clock);});
  const int old=std::fegetround();std::fesetround(FE_DOWNWARD);p[6]=32;
  rejects([&]{lines.update(0,p,markers,{});});check(std::fegetround()==FE_DOWNWARD,"ChainLine exception lost rounding mode");std::fesetround(old);
}
void scene() {
  FrameFixture x;const auto material=x.material(0xffffffff),chain=x.add(0x6b,parameters(8),{},{material});
  const auto marker=x.add(0x182,{0});const auto transform=x.add(0xc3,{f(1),f(1),f(1),0,0,0,f(2),0,0},{marker});
  const auto scene=x.add(0xc1,{}, {chain,transform}),view=x.viewport();x.g.nodes[view].inputs={scene};
  wz3::ScenePlayer player(x.p,x.g);auto frame=player.viewport(view,wz3::frame_environment(0),1);
  check(frame.effects.size()==1 && frame.effects[0].ribbon.has_value(),"scene did not defer ChainLine ribbon");
  near(frame.effects[0].ribbon->points[0][0],2,"effect ignored a marker written later in scene traversal");
  x.p.operators[chain].parameters=parameters();x.g.nodes[scene].inputs={chain,chain};wz3::ScenePlayer repeated(x.p,x.g);repeated.begin_frame(10);
  frame=repeated.viewport(view,wz3::frame_environment(0),1);
  check(frame.effects.size()==2 && frame.effects[0].ribbon->points!=frame.effects[1].ribbon->points,"repeated paint jobs must advance the same ChainLine memory");
  const auto alias=x.add(1,{}, {},{chain});x.g.nodes[scene].inputs={chain,alias};wz3::ScenePlayer separate(x.p,x.g);separate.begin_frame(10);
  frame=separate.viewport(view,wz3::frame_environment(0),1);
  check(frame.effects.size()==2 && frame.effects[0].ribbon->points==frame.effects[1].ribbon->points,"separate paint operators must own separate ChainLine instance chains");
}
void debris(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);unsigned count=0;
  for(std::size_t i=0;i<p.operators.size();++i)if(g.nodes[i].reachable && p.operators[i].command==0x6b) {
    wz3::ChainLines native;source::KEnvironment env;wz3::FrameClock clock;
    for(int t:{0,16,32,48,64,80,96,112,128,144,160}) {clock.advance(t);source::compare(native,env,p.operators[i].parameters,identity_markers(),clock,wz3::scene_identity());}
    ++count;
  }
  check(count==53,"Debris ChainLine fixture count changed");std::cout<<count<<" real Debris ChainLine recipes match archived handler\n";
  wz3::FramePlayer player(p,g);const auto frame=player.frame(g.names.at("root"),0,1280,640,0);
  check(frame.commands.size()==3,"Debris opening frame command count");
  const auto& scene=std::get<wz3::SceneFrame>(frame.commands[0].draw);
  check(scene.instances.size()==76 && scene.effects.size()==26,"Debris opening scene geometry/effect count");
  for(const auto& draw:scene.effects)check(draw.ribbon.has_value(),"Debris opening effect did not retain its ribbon");
}
}
int main(int argc,char** argv) {
  try {check(argc==2,"expected Debris project");oracle();boundaries();scene();debris(argv[1]);}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
