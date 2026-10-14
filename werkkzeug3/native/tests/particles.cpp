// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/particles.hpp"
#include "mesh_kernel.hpp"
#include <cfenv>
#include <fstream>
#include <iostream>

namespace source {
using sInt=int;using sU32=std::uint32_t;using sF32=float;using sF323=wz3::legacy::sVector;using sVector=wz3::legacy::sVector;
constexpr float sPI2F=wz3::legacy::sPI2F;
float sFSin(float v) {return wz3::legacy::sFSin(v);}
double sFMod(double a,double b) {return std::fmod(a,b);}
struct sMatrix:wz3::legacy::sMatrix {
  void InitDir(const sVector& v) {j.Init3(0,1,0);k=v;k.UnitSafe3();i.Cross3(j,k);i.UnitSafe3();j.Cross3(k,i);i.w=j.w=k.w=0;l.Init(0,0,0,1);}
};
struct KInstanceMem {bool Reset{};void* DeleteArray{};};
struct EngMesh {
  sMatrix* GetInstanceMatrices(int) {throw std::runtime_error("unexpected optimized instances in source oracle");}
  void UpdateInstanceCount(std::ptrdiff_t) {}
};
struct GenScene {EngMesh* DrawMesh{};struct Children {int Count{};GenScene* operator[](int) {return nullptr;}} Childs;};
struct GenSpline {void Eval(float,int,sMatrix&,float&) {throw std::runtime_error("unexpected GenSpline in source oracle");}};
struct KSpline {
  const wz3::SplineLibrary& splines;std::size_t id;
  void Eval(float time,sVector& v) {const auto a=splines.evaluate(id,time);v.Init(a[0],a[1],a[2],a[3]);}
};
struct KOp {void* Cache;int GetInputCount() {return 1;}KOp* GetInput(int) {return nullptr;}};
struct KEnvironment {
  void* instance{};bool initialized{};sVector Var[32];std::vector<wz3::SceneParticle> output;
  struct Stack {
    std::vector<sMatrix> values;
    Stack() {sMatrix m;m.Init();values.push_back(m);}
    void PushMul(const sMatrix& m) {sMatrix result;result.MulA(m,values.back());values.push_back(result);}
    void Pop() {values.pop_back();}
    const sMatrix& Top() {return values.back();}
  } ExecStack;
  template<class T>T* GetInst(KOp*) {auto* mem=static_cast<T*>(instance);mem->Reset=!initialized;initialized=true;return mem;}
};
struct EngineStub {void AddPaintJob(EngMesh*,const sMatrix&,int,int) {throw std::runtime_error("unexpected engine call");}} engine;
EngineStub* Engine=&engine;
constexpr unsigned KV_SELECT=3,KV_FRACTION=11;
void ExecSceneInputs(KOp*,KEnvironment* env,int) {
  const auto& m=env->ExecStack.Top();env->output.push_back({unsigned(env->Var[3].x),env->Var[11].x,
    {m.i.x,m.i.y,m.i.z,m.i.w,m.j.x,m.j.y,m.j.z,m.j.w,m.k.x,m.k.y,m.k.z,m.k.w,m.l.x,m.l.y,m.l.z,m.l.w}});
}
std::uint32_t sRandomSeed=0x74382381;
#define sMOBILE 1
#define __stdcall
#define sZONE(name)
#include "particles_oracle.inc"
#undef sZONE
#undef __stdcall
#undef sMOBILE
struct Fixture {
  ScenePartMem memory{};GenScene scene;KOp op{&scene};KEnvironment env;
  Fixture() {env.instance=&memory;}
  ~Fixture() {delete[] memory.Pos;}
  std::vector<wz3::SceneParticle> run(std::span<const std::uint32_t> parameters,KSpline* spline=nullptr) {
    std::array<std::uint32_t,30> p{};std::copy(parameters.begin(),parameters.end(),p.begin());
    const auto real=[&](unsigned i){return wz3::as_float(p[i]);};const auto vec=[&](unsigned i){sVector v;v.Init(real(i),real(i+1),real(i+2));return v;};
    env.output.clear();Exec_Scene_Particles(&op,&env,int(p[0]),int(p[1]),int(p[2]),vec(3),vec(6),vec(9),real(12),vec(13),vec(16),vec(19),real(22),real(23),vec(24),vec(27),spline);
    return env.output;
  }
};
}
namespace {
void equal(const std::vector<wz3::SceneParticle>& a,const std::vector<wz3::SceneParticle>& b) {
  check(a.size()==b.size(),"source/native particle count mismatch");
  for(unsigned i=0;i<a.size();++i) {
    check(a[i].index==b[i].index && a[i].fraction==b[i].fraction,"source/native particle index/phase mismatch");
    for(unsigned c=0;c<16;++c)if(std::abs(a[i].transform[c]-b[i].transform[c])>1e-5f)
      throw std::runtime_error("source/native particle transform mismatch at "+std::to_string(i)+":"+std::to_string(c));
  }
}
wz3::Project splined() {
  wz3::Project p;wz3::Spline spline;spline.name="path";spline.interpolation=0;
  spline.channels={{{f(0),f(0)},{f(1),f(4)}},{{f(0),f(0)},{f(1),f(0)}},{{f(0),f(0)},{f(1),f(0)}}};p.splines.push_back(spline);return p;
}
void oracle() {
  const auto p=splined();const wz3::SplineLibrary splines(p);wz3::SceneParticles native(splines);source::KSpline spline{splines,0};
  // Compile and execute the unmodified source handler against a portable host.
  // Matrix and curve primitives are shared; execution, RNG and caching are not.
  for(unsigned mode=0;mode<256;++mode) {
    source::Fixture original;std::array<std::uint32_t,30> values{mode,13,mode};
    for(unsigned i=3;i<30;++i)values[i]=f(float(i)*.02f);values[23]=f(.1f);
    for(float time:{-.25f,0.0f,.125f,.75f,1.25f,2.5f}) {
      values[12]=f(time);equal(native.evaluate(mode,values,0),original.run(values,&spline));
    }
    for(unsigned index:{16,17,18,19,22,23,24,27,29,3,6}) {
      values[index]=f(.7f);equal(native.evaluate(mode,values,0),original.run(values,&spline));
    }
  }
  std::cout<<"256 particle mode combinations match extracted source over motion and cache updates\n";
}
void analytic() {
  const auto p=splined();const wz3::SplineLibrary splines(p);wz3::SceneParticles native(splines);
  std::array<std::uint32_t,30> values{0x20,4,0};values[12]=f(.125f);values[13]=f(8);
  auto particles=native.evaluate(0,values);check(particles.size()==4,"ordered particles count");
  for(unsigned i=0;i<4;++i) {near(particles[i].fraction,.125f+.25f*i,"ordered phase");near(particles[i].transform[12],1+2*i,"line placement");}
  values[0]=0x61;values[12]=f(.5f);values[13]=f(8);particles=native.evaluate(0,values,0);
  check(particles.size()==2 && particles[0].index==2 && particles[1].index==3,"come-in keeps original particle indices");
  near(particles[1].transform[12],3,"named spline and line displacement are both added");
  values[0]=0x31;values[12]=0;values[13]=0;values[11]=f(wz3::legacy::sPI2F);particles=native.evaluate(0,values,0);
  const auto& rotated=particles[1].transform;
  near(rotated[0],0,"rotation is radians around spline orientation");near(rotated[1],1,"particle local X rotates into spline up");near(rotated[8],1,"spline forward follows positive X");
  values[0]=0xa0;values[12]=f(-2.25f);values[11]=0;values[13]=f(8);particles=native.evaluate(0,values);
  near(particles[0].fraction,-1.25f,"nonlooping negative animation adds one only once");
  const auto rounding=std::fegetround();std::fesetround(FE_DOWNWARD);native.evaluate(0,values);check(std::fegetround()==FE_DOWNWARD,"particle evaluation must restore caller rounding");std::fesetround(rounding);
  values[1]=0;check(native.evaluate(0,values).empty(),"zero particle count");
}
void scene_variables() {
  FrameFixture x;const auto view=x.viewport(),cube=x.g.nodes[view].inputs[0];
  std::vector<std::uint32_t> srt={f(1),f(1),f(1),0,0,0,0,0,0};
  const auto fraction=x.add(0xc3,srt,{cube});x.animate(fraction,11,7);
  const auto selection=x.add(0xc3,srt,{fraction});x.animate(selection,3,8);
  std::vector<std::uint32_t> params(16);params[0]=0x20;params[1]=4;params[12]=f(.125f);params[13]=f(8);
  const auto particles=x.add(0xc5,params,{selection});const auto all=x.add(0xc1,{}, {particles,selection});x.g.nodes[view].inputs={all};
  wz3::ScenePlayer player(x.p,x.g);auto env=wz3::frame_environment(0);env.variables[3].fill(23);env.variables[11].fill(.875f);
  const auto scene=player.viewport(view,env,1);check(scene.instances.size()==5,"particle child scene instancing");
  for(unsigned i=0;i<4;++i) {near(scene.instances[i].world[12],1+2*i,"particle parent transform");near(scene.instances[i].world[13],.125f+.25f*i,"child reads KV_FRACTION");near(scene.instances[i].world[14],float(i),"child reads KV_SELECT");check(scene.instances[i].mesh==scene.instances[4].mesh,"particles share static geometry");}
  near(scene.instances[4].world[13],.875f,"fraction restored before sibling");near(scene.instances[4].world[14],23,"selection restored before sibling");
  x.p.runtime_graph=true;wz3::ScenePlayer released(x.p,x.g);
  env=wz3::frame_environment(0);env.variables[3].fill(23);env.variables[11].fill(.875f);
  const auto old=released.viewport(view,env,1);
  for(unsigned i=0;i<4;++i) {
    near(old.instances[i].world[13],.875f,"released particles leave KV_FRACTION unchanged");
    near(old.instances[i].world[14],23,"released particles leave KV_SELECT unchanged");
  }
}
void released() {
  const auto p=splined();const wz3::SplineLibrary splines(p);wz3::SceneParticles native(splines,{},true);
  // First three positions of release operator 133, independently obtained
  // from x86 0x820782..0x820895 by release_particle_oracle.py (seed zero).
  const std::array<std::array<float,4>,3> expected{{
    {.8926759362220764f,1.5656044483184814f,-.03581085801124573f,.2251311093568802f},
    {1.2234467267990112f,.8877468705177307f,.438912570476532f,.592547595500946f},
    {-1.1036046743392944f,1.3614574670791626f,.49032723903656006f,.5563706159591675f}}};
  std::array<std::uint32_t,16> values{0,150,0,f(2.76953125f),f(4.28125f),f(1)};
  const auto cloud=native.evaluate(0,values);
  for(unsigned i=0;i<expected.size();++i) {
    for(unsigned c=0;c<3;++c)near(cloud[i].transform[12+c],expected[i][c],"released x86 particle position");
    near(cloud[i].fraction,expected[i][3],"released x86 particle phase");
  }
  values={0x31,4,0};values[12]=f(.125f);values[13]=f(100);values[11]=f(5);
  const auto line=native.evaluate(1,values),curve=native.evaluate(2,values,0);
  for(unsigned i=0;i<4;++i) {
    const auto phase=.125f+.25f*i;
    near(line[i].transform[12],100*phase,"released line placement");
    near(curve[i].transform[12],4*phase,"released spline excludes line displacement");
    near(curve[i].transform[8],1,"released spline orientation");
    near(line[i].transform[0],1,"released handler ignores later angular velocities");
  }
}
void released_oracle(const char* path) {
  std::ifstream in(path);unsigned cases=0,total=0;float max_error=0;in>>cases;check(in.good() && cases>0 && cases<=100,"invalid released cloud oracle");
  wz3::Project p;const wz3::SplineLibrary splines(p);wz3::SceneParticles native(splines,{},true);
  for(unsigned id=0;id<cases;++id) {
    std::array<std::uint32_t,16> values{};in>>values[0]>>values[1]>>values[2];
    for(unsigned c=3;c<6;++c){float v=0;in>>v;values[c]=f(v);}
    check(in.good() && values[1]>0 && values[1]<=16384,"invalid released cloud parameters");
    const auto result=native.evaluate(id,values);total+=result.size();
    for(const auto& particle:result) {
      std::array<float,4> expected{};for(auto& value:expected)in>>value;
      check(in.good(),"truncated released cloud oracle");
      for(unsigned c=0;c<3;++c)max_error=std::max(max_error,std::abs(particle.transform[12+c]-expected[c]));
      max_error=std::max(max_error,std::abs(particle.fraction-expected[3]));
    }
  }
  in>>std::ws;check(in.eof(),"trailing released cloud data");
  check(max_error<=1e-6f,"native/cloud x86 values differ by more than 1e-6");
  std::cout<<total<<" particles in "<<cases<<" released clouds match isolated x86 initializer; max absolute error "<<max_error<<'\n';
}
void released_motion() {
  wz3::Project p;p.runtime_graph=true;wz3::Spline curve;curve.name="released path";curve.interpolation=0;
  // Released spline 3 and operator 1224. These poses come from the original
  // complete x86 handler, not the archived source oracle's shared primitives.
  const auto key=[](unsigned time,float value){return std::array<std::uint32_t,2>{f(float(time)/255),f(value)};};
  curve.channels={{key(0,0),key(33,58.3125f),key(51,72.0625f),key(77,64.25f),key(103,34.375f),
                   key(143,-20.8125f),key(195,-77.4375f),key(235,-42.46875f),key(255,0)},
                  {key(0,0),key(255,0)},
                  {key(0,71.5f),key(17,62.21875f),key(125,-82.3125f),key(236,61.5f),key(255,71.5f)}};
  p.splines.push_back(curve);const wz3::SplineLibrary splines(p);wz3::SceneParticles native(splines,{},true);
  std::array<std::uint32_t,16> parameters{0x31,178,0,f(1),f(8.046875f),f(1)};parameters[12]=f(.125f);
  const auto result=native.evaluate(1224,parameters,0);
  const std::array<unsigned,3> indices{0,63,177};
  const std::array<wz3::SceneMatrix,3> poses{{
    {-.7534292936325073f,0,-.6575290560722351f,0,0,1,0,0,.6575289964675903f,0,-.7534291744232178f,0,
      57.01850891113281f,2.942650556564331f,41.10980987548828f,1},
    {-.05833634361624718f,0,.9982970356941223f,0,0,1,0,0,-.9982969164848328f,0,-.05833633616566658f,0,
      7.765969276428223f,-2.539154052734375f,-82.27741241455078f,1},
    {-.7314648628234863f,0,-.6818791031837463f,0,0,1,0,0,.6818791031837463f,0,-.7314648628234863f,0,
      54.27203369140625f,-1.905017614364624f,43.73930740356445f,1}}};
  for(unsigned i=0;i<indices.size();++i)for(unsigned c=0;c<16;++c)
    check(std::abs(result[indices[i]].transform[c]-poses[i][c])<=1e-4f,"released x86 spline particle pose");
  // Negative time wraps before the initial phase is added, then the complete
  // transform is multiplied by its parent (operator 133 at animation -0.01).
  parameters={0,150,0,f(2.76953125f),f(4.28125f),f(1)};parameters[12]=f(-.01f);parameters[15]=f(110.5f);
  const auto line=native.evaluate(133,parameters);
  const wz3::SceneMatrix parent{1,.25f,-.5f,0,-.125f,.75f,.125f,0,.25f,.5f,1.5f,0,-3,2,5,1};
  const auto world=wz3::scene_combine(parent,line[0].transform);
  check(f(line[0].fraction)==f(.21513116359710693f),"released x86 negative-time particle phase");
  near(world[12],3.631021022796631f,"released x86 parent translation X");
  near(world[13],15.265464782714844f,"released x86 parent translation Y");
  near(world[14],40.3536376953125f,"released x86 parent translation Z");
}
void released_motion_oracle(const char* project,const char* path) {
  const auto p=wz3::read_project(project);check(p.runtime_graph,"particle motion oracle requires a released runtime graph");
  const wz3::SplineLibrary splines(p);wz3::SceneParticles native(splines,{},true);
  std::ifstream in(path);std::string magic;unsigned cases=0,total=0,exact=0;float max_error=0;
  in>>magic>>cases;check(in.good() && magic=="theta-particle-motion-v1" && cases>0 && cases<=4096,"invalid released motion oracle");
  for(unsigned trial=0;trial<cases;++trial) {
    std::size_t id=0;std::uint32_t time=0;unsigned count=0;wz3::SceneMatrix world{};
    in>>id>>time>>count;for(auto& value:world)in>>value;
    check(in.good() && id<p.operators.size() && count>0 && count<=16384,"invalid released motion case");
    const auto& op=p.operators[id];check(op.command==0xc5 && op.parameters.size()==16,"invalid released motion operator");
    auto parameters=op.parameters;parameters[12]=time;std::optional<std::size_t> curve;
    if(!op.splines.empty() && !op.splines[0].empty())curve=splines.find(op.splines[0]);
    const auto result=native.evaluate(id,parameters,curve);check(result.size()==count,"released motion particle count mismatch");
    for(unsigned i=0;i<count;++i) {
      std::array<std::uint32_t,17> expected{};for(auto& value:expected)in>>value;
      check(in.good(),"truncated released motion oracle");
      const auto& particle=result[i];check(particle.index==i && f(particle.fraction)==expected[0],"released motion particle index/phase mismatch");
      const auto actual=wz3::scene_combine(world,particle.transform);
      for(unsigned c=0;c<16;++c) {
        const auto value=wz3::as_float(expected[c+1]),error=std::abs(actual[c]-value);
        check(std::isfinite(value) && std::isfinite(actual[c]),"nonfinite released motion matrix");
        max_error=std::max(max_error,error);exact+=f(actual[c])==expected[c+1];
        if(error>1e-4f+2e-6f*std::abs(value))
          throw std::runtime_error("released motion mismatch: operator "+std::to_string(id)+", case "+std::to_string(trial)+
            ", particle "+std::to_string(i)+", component "+std::to_string(c)+", expected "+std::to_string(value)+", got "+std::to_string(actual[c]));
      }
    }
    total+=count;
  }
  in>>std::ws;check(in.eof(),"trailing released motion data");
  std::cout<<total<<" particle transforms in "<<cases<<" cases match original x86 motion; "<<exact<<'/'<<total*16
    <<" matrix components bitwise equal; max absolute error "<<max_error<<'\n';
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);const wz3::SplineLibrary splines(p);wz3::SceneParticles native(splines);unsigned count=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0xc5) {
    ++count;const auto& op=p.operators[id];std::optional<std::size_t> curve;
    if(!op.splines.empty() && !op.splines[0].empty())curve=splines.find(op.splines[0]);
    source::Fixture original;source::KSpline spline{splines,curve.value_or(0)};
    for(float time:{0.0f,.125f,.5f,.875f,1.0f}) {
      auto env=wz3::frame_environment(std::int32_t(time*65536));const wz3::AnimationScope animation(wz3::compile_animation(op,splines),splines,op.parameters,env);
      equal(native.evaluate(id,animation.parameters,curve),original.run(animation.parameters,curve ? &spline : nullptr));
    }
  }
  check(count==9,"Theta reachable scene particle count");std::cout<<"All 9 Theta Particles operators match extracted source at five times\n";
}
}
int main(int argc,char** argv) {
  try {
    if(argc==3 && std::string_view(argv[1])=="--release-oracle")released_oracle(argv[2]);
    else if(argc==4 && std::string_view(argv[1])=="--release-motion-oracle")released_motion_oracle(argv[2],argv[3]);
    else {oracle();analytic();released();released_motion();scene_variables();if(argc>1)theta(argv[1]);}
  }
  catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
