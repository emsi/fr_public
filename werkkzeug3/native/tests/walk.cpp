// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/walk.hpp"
#include "mesh_kernel.hpp"
#include <cfenv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <iostream>

namespace source {
using sInt=int;using sU32=std::uint32_t;using sF32=float;using sVector=wz3::legacy::sVector;using sMatrix=wz3::legacy::sMatrix;
using sF323=sVector;using sF322=sVector;struct sInt2{int x,y;};
constexpr float sPI=wz3::legacy::sPI;constexpr unsigned KV_LEG_L0=16,KV_LEG_TIMES=27;
int sFtol(float value) {return int(std::nearbyint(value));}
float sFSin(float v) {return wz3::legacy::sFSin(v);}float sFSqrt(float v) {return std::sqrt(v);}
int sMin(int a,int b) {return std::min(a,b);}float sRange(float v,float high,float low) {return std::clamp(v,low,high);}
wz3::SceneMatrix matrix(const sMatrix& m) {return {m.i.x,m.i.y,m.i.z,m.i.w,m.j.x,m.j.y,m.j.z,m.j.w,m.k.x,m.k.y,m.k.z,m.k.w,m.l.x,m.l.y,m.l.z,m.l.w};}
sMatrix matrix(const wz3::SceneMatrix& m) {sMatrix out;out.i.Init(m[0],m[1],m[2],m[3]);out.j.Init(m[4],m[5],m[6],m[7]);out.k.Init(m[8],m[9],m[10],m[11]);out.l.Init(m[12],m[13],m[14],m[15]);return out;}
struct KInstanceMem {bool Reset{};};
struct KSpline {const wz3::SplineLibrary& library;std::size_t id;void Eval(float time,sVector& v){const auto p=library.evaluate(id,time);v.Init(p[0],p[1],p[2],p[3]);}};
struct Snapshot {wz3::SceneMatrix world;std::array<wz3::AnimationVector,32> variables;};
struct KEnvironment {
  sVector Var[32];int CurrentTime{};bool TimeReset{};void* memory{};bool initialized{};std::vector<Snapshot> output;
  struct {sMatrix CameraSpace;} CurrentCam;
  struct Stack {
    std::vector<sMatrix> values;
    Stack(){values.reserve(512);sMatrix m;m.Init();values.push_back(m);}
    sMatrix& Top(){return values.back();}void Push(const sMatrix& m){values.push_back(m);}void Pop(){values.pop_back();}
    void PushMul(const sMatrix& m){sMatrix out;out.MulA(m,Top());Push(out);}
  } ExecStack;
  template<class T>T* GetInst(void*) {auto* m=static_cast<T*>(memory);m->Reset=!initialized;initialized=true;return m;}
  void SetMatrix(sMatrix& m,sMatrix& saved) {saved.i=Var[28];saved.j=Var[29];saved.k=Var[30];saved.l=Var[31];Var[28]=m.i;Var[29]=m.j;Var[30]=m.k;Var[31]=m.l;}
  void RestoreMatrix(sMatrix& saved) {Var[28]=saved.i;Var[29]=saved.j;Var[30]=saved.k;Var[31]=saved.l;}
  void capture(){Snapshot s;s.world=matrix(ExecStack.Top());for(unsigned i=0;i<32;++i)s.variables[i]={Var[i].x,Var[i].y,Var[i].z,Var[i].w};output.push_back(s);}
};
struct KOp {int GetInputCount(){return 2;}void ExecInputs(KEnvironment* e){e->capture();}};
void ExecSceneInput(KOp*,KEnvironment* e,int){e->capture();}
#define __stdcall
#define sZONE(x)
#define sINTRO 0
#include "walk_oracle.inc"
#undef sINTRO
#undef sZONE
#undef __stdcall
struct Fixture {
  WalkMem memory{};KEnvironment env;KOp op;
  Fixture(){env.memory=&memory;env.CurrentCam.CameraSpace.Init();}
  Snapshot walk(std::span<const std::uint32_t> p,const wz3::SceneMatrix& world,const wz3::FrameClock& clock,KSpline* spline=nullptr) {
    const auto real=[&](unsigned i){return wz3::as_float(p[i]);};const auto vec=[&](unsigned i){sVector v;v.Init(real(i),real(i+1),real(i+2),0);return v;};
    env.CurrentTime=clock.current;env.TimeReset=clock.reset;env.ExecStack.Top()=matrix(world);env.output.clear();
    Exec_Scene_Walk(&op,&env,p[0],p[1],real(2),real(3),real(4),{int(p[5]),int(p[6])},vec(7),vec(9),vec(11),vec(13),vec(16),vec(19),vec(22),vec(25),vec(28),vec(31),vec(34),real(37),real(38),spline);
    check(env.output.size()==1,"source Walk child count");return env.output[0];
  }
  std::vector<Snapshot> limb(std::span<const std::uint32_t> p,const wz3::SceneMatrix& world) {
    const auto real=[&](unsigned i){return wz3::as_float(p[i]);};sVector pos,dir;pos.Init(real(0),real(1),real(2),0);dir.Init(real(3),real(4),real(5),0);env.ExecStack.Top()=matrix(world);env.output.clear();
    Exec_Scene_Limb(&op,&env,pos,dir,real(6),real(7),p[8]);return env.output;
  }
};
}
namespace {
void close(float a,float b,const char* message) {check(std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<.0001f,message);}
void equal(const wz3::SceneMatrix& a,const wz3::SceneMatrix& b) {for(unsigned i=0;i<16;++i)close(a[i],b[i],"source/native matrix mismatch");}
std::vector<std::uint32_t> walk_parameters() {
  std::vector<std::uint32_t> p(39);p[0]=8;p[1]=4;p[2]=f(.14f);p[3]=f(.75f);p[4]=f(1.25f);p[5]=4;p[6]=2;p[7]=f(1.5f);p[8]=f(2);p[9]=f(.5f);p[10]=f(.25f);p[11]=f(.2f);p[12]=f(.125f);
  const float foot[4][3]={{-1,.25f,-2},{1,.25f,-2},{1,.25f,2},{-1,.25f,2}};
  for(unsigned i=0;i<4;++i)for(unsigned c=0;c<3;++c)p[13+i*3+c]=f(foot[i][c]);p[37]=p[38]=f(2);return p;
}
wz3::SceneMatrix path(int time) {
  const auto t=time/1000.f;const std::array<std::uint32_t,9> p={f(1),f(1),f(1),0,f(std::sin(t)*.1f),0,f(std::sin(t)*2),f(1),f(t<2 ? t*.1f : t<4 ? (t-2)*8+.2f : 16.2f)};return wz3::scene_srt(p);
}
wz3::Project splined() {
  wz3::Project p;wz3::Spline spline;spline.name="step";spline.interpolation=0;
  spline.channels={{{f(0),f(0)},{f(.25f),f(.2f)},{f(.5f),f(0)},{f(.75f),f(.1f)},{f(1),f(0)}},{{f(0),f(0)},{f(.25f),f(.4f)},{f(.5f),f(0)},{f(.75f),f(.6f)},{f(1),f(0)}},{{f(0),f(0)},{f(1),f(0)}},{{f(0),f(0)},{f(.5f),f(1)},{f(.5001f),f(1)},{f(1),f(2)}}};p.splines.push_back(spline);return p;
}
void gait_oracle() {
  const auto project=splined();const wz3::SplineLibrary splines(project);source::KSpline spline{splines,0};unsigned comparisons=0;
  for(unsigned flags=0;flags<512;flags+=1) {
    auto p=walk_parameters();p[0]=flags;wz3::SceneWalk native(splines);source::Fixture original;wz3::FrameClock clock;
    const bool use_spline=flags&1;
    for(int ms=0;ms<=6000;ms+=37) {
      clock.advance(ms);const auto world=path(ms);const auto pose=native.evaluate(0,p,world,clock,use_spline ? std::optional<std::size_t>(0) : std::nullopt);
      const auto expected=original.walk(p,world,clock,use_spline ? &spline : nullptr);equal(pose.body,expected.world);
      for(unsigned i=0;i<pose.count;++i)for(unsigned c=0;c<4;++c)close(pose.feet[i][c],expected.variables[16+i][c],"source/native foot mismatch");
      for(unsigned c=0;c<4;++c)close(pose.timing[c],expected.variables[27][c],"source/native step phase mismatch");++comparisons;
    }
    clock.advance(100);equal(native.evaluate(0,p,path(100),clock).body,original.walk(p,path(100),clock).world);
  }
  std::cout<<comparisons<<" gait poses matched extracted source across all 512 flag combinations\n";
}
void counts_and_validation() {
  const wz3::Project project;const wz3::SplineLibrary splines(project);
  for(unsigned count=1;count<=8;++count) {
    auto p=walk_parameters();p[0]=0;p[1]=count;p[5]=count;p[6]=1;
    for(unsigned i=0;i<count;++i) {p[13+i*3]=f(float(i)-count*.5f);p[14+i*3]=f(.25f);p[15+i*3]=f((i&1) ? -2.f : 2.f);}
    wz3::SceneWalk native(splines);source::Fixture original;wz3::FrameClock clock;
    for(int ms=0;ms<3000;ms+=50) {clock.advance(ms);const auto pose=native.evaluate(0,p,path(ms),clock);const auto expected=original.walk(p,path(ms),clock);equal(pose.body,expected.world);for(unsigned i=0;i<count;++i)for(unsigned c=0;c<4;++c)close(pose.feet[i][c],expected.variables[16+i][c],"foot-count source mismatch");}
  }
  const auto rejected=[&](std::vector<std::uint32_t> p) {wz3::SceneWalk native(splines);bool caught=false;try {native.evaluate(0,p,wz3::scene_identity(),{});}catch(const std::runtime_error&){caught=true;}check(caught,"invalid gait parameters accepted");};
  auto p=walk_parameters();p[5]=0;rejected(p);p=walk_parameters();p[1]=9;rejected(p);p=walk_parameters();p[9]=f(.0001f);rejected(p);p=walk_parameters();p[11]=0;rejected(p);
  const std::vector<std::uint32_t> limb={f(1),f(-3),0,f(1),0,0,f(2),f(2),0};const auto pose=wz3::scene_limb(limb,wz3::scene_identity());
  const auto upper_joint=wz3::scene_point(pose.world[0],{0,-2,0}),lower_joint=wz3::scene_point(pose.world[1],{0,2,0});
  for(unsigned c=0;c<3;++c)near(upper_joint[c],lower_joint[c],"reachable two-bone limb must meet at the knee");
}
void limbs_and_rotation() {
  source::Fixture original;const auto world=path(1250);
  for(unsigned flags=0;flags<4;++flags)for(float y:{-1.f,-3.f,-7.f})for(float x:{-2.f,2.f}) {
    const std::vector<std::uint32_t> p={f(x),f(y),f(3),f(1),0,f(.2f),f(2),f(3),flags};
    const auto actual=wz3::scene_limb(p,world);const auto expected=original.limb(p,world);check(expected.size()==2,"source Limb child count");
    for(unsigned i=0;i<2;++i) {equal(actual.world[i],expected[i].world);for(unsigned c=0;c<16;++c)close(actual.other[i][c],expected[i].variables[28+c/4][c%4],"source/native Limb matrix variable mismatch");}
  }
  wz3::SceneCamera camera;camera.world=path(500);original.env.CurrentCam.CameraSpace=source::matrix(camera.world);
  for(unsigned axis=0;axis<4;++axis) {
    const std::array<std::uint32_t,4> p={f(.2f),f(.6f),f(.9f),axis};source::sVector dir;dir.Init(.2f,.6f,.9f,0);
    original.env.ExecStack.Top()=source::matrix(world);original.env.output.clear();source::Exec_Scene_Rotate(&original.op,&original.env,dir,axis);
    equal(wz3::scene_rotate(p,world,camera),original.env.output[0].world);
  }
}
void variables_and_instances() {
  FrameFixture x;const auto viewport=x.viewport();const auto cube=x.g.nodes[viewport].inputs[0];
  const auto child=x.add(0xc3,{f(1),f(1),f(1),0,0,0,0,0,0},{cube});x.animate(child,16,6);
  const auto walk=x.add(0xc8,walk_parameters(),{child});const auto scene=x.add(0xc1,{}, {walk,child});x.g.nodes[viewport].inputs={scene};
  wz3::ScenePlayer player(x.p,x.g);auto env=wz3::frame_environment(0);env.variables[16]={9,8,7,6};env.variables[27]={4,3,2,1};
  player.begin_frame(0);const auto a=player.viewport(viewport,env,1);check(a.instances.size()==2,"Walk child traversal");
  near(a.instances[0].world[12],-1,"Walk supplies foot position to child animation");near(a.instances[1].world[12],9,"Walk restores foot variables for its sibling");
  // Distinct event instances must not share planted feet or scheduler memory.
  FrameFixture events;const auto vp=events.viewport(),mesh=events.g.nodes[vp].inputs[0];const auto walker=events.add(0xc8,walk_parameters(),{mesh});
  const auto first=events.event(walker,"a",0,65536),second=events.event(walker,"b",0,65536);events.p.events[1].transform[6]=f(20);
  events.g.nodes[vp].inputs={events.add(0xc1,{}, {first,second})};const auto root=events.add(0xd,{}, {vp});wz3::FramePlayer frame_player(events.p,events.g);
  const auto frame=frame_player.frame(root,0,32,32,0);const auto& poses=std::get<wz3::SceneFrame>(frame.commands[0].draw).instances;
  check(poses.size()==2,"two walker event instances");near(poses[1].world[12]-poses[0].world[12],20,"walker state must be event-owned");
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::FramePlayer player(p,g);const auto root=g.names.at("root");
  std::size_t count=0;std::vector<wz3::SceneMatrix> previous;bool moved=false;
  // Exercise the whole walking branch, not just a cold midpoint pose.
  for(int ms=191000;ms<=222500;ms+=100) {
    const auto beat=std::int32_t(double(ms)*wz3::as_float(p.settings[4])*65536/60000);const auto frame=player.frame(root,beat,160,120,ms);
    for(const auto& command:frame.commands)if(const auto* scene=std::get_if<wz3::SceneFrame>(&command.draw)) {
      check(!scene->instances.empty(),"Theta walking scene missing geometry");count=scene->instances.size();
      std::vector<wz3::SceneMatrix> worlds;for(const auto& instance:scene->instances)worlds.push_back(instance.world);
      if(!previous.empty() && previous!=worlds)moved=true;previous=std::move(worlds);
    }
  }
  check(moved,"Theta walking animation did not change poses");std::cout<<"Theta walking branch emitted "<<count<<" instances with changing poses\n";
}
void release_trace(const char* project_path,const char* output_path) {
  const auto project=wz3::read_project(project_path);check(project.runtime_graph,"release trace needs a runtime project");
  const wz3::SplineLibrary splines(project);
  const wz3::Operator* op=nullptr;
  for(const auto& candidate:project.operators)if(candidate.command==0xc8) {check(!op,"ambiguous released Walk");op=&candidate;}
  check(op && op->splines.size()==1,"missing released Walk/spline");const auto spline=splines.find(op->splines[0]);check(spline.has_value(),"missing released gait spline");
  std::ofstream out(output_path);out<<std::setprecision(std::numeric_limits<float>::max_digits10);
  for(int step:{16,33,50}) {
    wz3::SceneWalk native(splines);wz3::FrameClock clock;
    for(int ms=0;;ms=std::min(ms+step,6000)) {
      clock.advance(ms);const auto world=path(ms);const auto pose=native.evaluate(0,op->parameters,world,clock,spline);
      out<<step<<' '<<ms;for(float value:world)out<<' '<<value;for(float value:pose.body)out<<' '<<value;
      for(unsigned i=0;i<pose.count;++i)for(float value:pose.feet[i])out<<' '<<value;
      for(float value:pose.timing)out<<' '<<value;out<<'\n';
      if(ms==6000)break;
    }
  }
  check(out.good(),"cannot write release Walk trace");
}
}
int main(int argc,char** argv) {
  try {
    std::fesetround(FE_TONEAREST);
    if(argc==4 && std::string_view(argv[1])=="--release-trace")release_trace(argv[2],argv[3]);
    else {gait_oracle();counts_and_validation();limbs_and_rotation();variables_and_instances();if(argc>1)theta(argv[1]);}
  }
  catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
