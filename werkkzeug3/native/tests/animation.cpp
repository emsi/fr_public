// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/animation.hpp"
#include "wz3/graph.hpp"
#include <bit>
#include <cfenv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void near(float a,float b,const char* message) {check(std::abs(a-b)<.00001f,message);}
std::uint32_t f(float v) {return std::bit_cast<std::uint32_t>(v);}
template<class F> void rejects(F fn) {bool caught=false;try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid animation accepted");}
wz3::Animation node(int x,int y,int width,unsigned cmd,unsigned parameter=0,std::vector<std::uint32_t> values={},unsigned automatic=0,std::string name={}) {
  wz3::Animation a;const unsigned kind=cmd>=0x80 ? 5 : cmd>=0x16 && cmd<=0x19 ? cmd-0x15 : cmd==0x1d ? 6 : cmd==0x1e ? 7 : 0;
  a.fields={std::uint32_t(x),std::uint32_t(y),std::uint32_t(width),cmd,parameter,kind,automatic,0};a.values=std::move(values);a.name=std::move(name);return a;
}
wz3::Operator unary(unsigned cmd,wz3::AnimationVector input,std::vector<std::uint32_t> values={}) {
  wz3::Operator op;op.parameters.resize(4);
  op.animations={node(0,0,3,0x16,0,{f(input[0]),f(input[1]),f(input[2]),f(input[3])}),node(0,1,3,cmd,0,std::move(values)),node(0,2,3,0x9f)};return op;
}
wz3::AnimationVector eval(const wz3::Operator& op,wz3::AnimationEnvironment env={}) {
  const wz3::SplineLibrary splines(wz3::Project{});const auto program=wz3::compile_animation(op,splines);
  const wz3::AnimationScope result(program,splines,op.parameters,env);wz3::AnimationVector value{};
  for(unsigned i=0;i<4;++i)value[i]=wz3::as_float(result.parameters[i]);return value;
}
void compiler() {
  const wz3::SplineLibrary splines(wz3::Project{});wz3::Operator op;op.parameters.resize(4);
  op.animations={node(0,2,3,0x97),node(0,0,3,2),node(0,1,3,0x18,0,{f(2)})};
  const auto program=wz3::compile_animation(op,splines);
  check(program.code==std::vector<std::uint8_t>({2,0,0x18,0,0,0,0x40,0x0d,0x97,0,1}),"compiler bytecode golden and automatic multiply");
  auto env=wz3::frame_environment(16384);
  {const wz3::AnimationScope result(program,splines,op.parameters,env);near(wz3::as_float(result.parameters[0]),.5f,"compiled time expression");check(result.parameters[3]==0,"write mask changed W");}
  for(unsigned automatic=0;automatic<4;++automatic) {
    op.animations[2].fields[6]=automatic;const auto v=eval(op,env);
    near(v[0],automatic==0 ? .5f : automatic==1 ? 2.25f : automatic==2 ? -1.75f : .125f,"automatic input/constant operand order");
  }
  op.animations={node(3,0,3,0x18,0,{f(3)}),node(0,0,3,0x18,0,{f(7)}),node(0,1,6,0x0c),node(0,2,6,0x9f)};
  near(eval(op)[0],4,"spatial binary input order");
  op.animations.push_back(node(20,0,3,0x18,0,{f(99)}));
  check(wz3::compile_animation(op,splines).used_nodes==4,"disconnected editor node must not enter bytecode");
  op.animations.push_back(node(0,1,6,0x9f));rejects([&]{wz3::compile_animation(op,splines);});

  op.animations={node(0,0,3,0x18,0,{f(7)}),node(0,1,3,0x91),node(5,2,3,3),node(5,3,3,0x91,1)};
  op.parameters={f(2),0,0,0};const auto value=eval(op);
  near(value[0],7,"parameter store");near(value[1],2,"LOADPARA must read original data after a store");
  op.animations={node(0,0,3,0x18,0,{f(1)}),node(0,1,3,0x97,3)};rejects([&]{wz3::compile_animation(op,splines);});
  op.animations={node(0,0,3,2,32),node(0,1,3,0x91)};rejects([&]{wz3::compile_animation(op,splines);});
  op.animations={node(0,0,3,2,1),node(0,1,3,0x8f,1),node(4,2,3,2,1),node(4,3,3,0x91)};
  rejects([&]{wz3::compile_animation(op,splines);});
}
void scope_and_storage() {
  const wz3::SplineLibrary splines(wz3::Project{});wz3::Operator parent;parent.parameters.resize(4);
  parent.animations={node(0,0,3,0x16,0,{f(1),f(2),f(3),f(4)}),node(0,1,3,0x85,3)};
  auto child=parent;child.animations[0].values={f(5),f(6),f(7),f(8)};
  auto env=wz3::frame_environment(0);env.variables[3]={10,20,30,40};
  {
    const wz3::AnimationScope outer(wz3::compile_animation(parent,splines),splines,parent.parameters,env);
    check(env.variables[3]==wz3::AnimationVector{1,20,3,40},"masked variable store");
    {const wz3::AnimationScope inner(wz3::compile_animation(child,splines),splines,child.parameters,env);check(env.variables[3][0]==5,"nested variable store");}
    check(env.variables[3]==wz3::AnimationVector{1,20,3,40},"nested scope restore");
  }
  check(env.variables[3]==wz3::AnimationVector{10,20,30,40},"parent scope restore");
  auto bad=wz3::compile_animation(parent,splines);bad.code.back()=0xff;
  rejects([&]{wz3::AnimationScope result(bad,splines,parent.parameters,env);});
  check(env.variables[3]==wz3::AnimationVector{10,20,30,40},"exception must restore stored variables");

  wz3::Operator op;op.parameters={0xaabbccddu};op.animations={node(0,0,3,0x17,0,{0x80402010}),node(0,1,3,0xbf)};
  {const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env);check(value.parameters[0]==0x80402010,"color constant and byte store must preserve BGRA order");}
  op.animations[1].fields[3]=0xb5;
  {const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env);check(value.parameters[0]==0xaa40cc10,"byte mask must preserve untouched color channels");}
  op.animations={node(0,0,3,0x18,0,{f(3.25f)}),node(0,1,3,0xe1)};op.parameters={3};
  {const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env);check(value.parameters[0]==3 && value.changed,"integer change compares before truncation");}
  op.animations[0].values={f(-3.75f)};
  {const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env);check(value.parameters[0]==0xfffffffdu,"integer store truncates toward zero");}
  op.animations[0].values={f(.5f)};op.animations[1].fields[3]=0xf1;op.parameters={0};
  const std::vector<std::uint32_t> previous={127};
  {const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env,previous);check(value.parameters[0]==127 && !value.changed,"byte change uses previous animated values");}

  for(auto code:std::vector<std::vector<std::uint8_t>>{{0x0b,1},{0x18,0},{2,99,0x91,0,1},{1,0},{0x19,0,0,1}})
    rejects([&]{wz3::AnimationProgram p;p.code=code;wz3::AnimationScope result(p,splines,op.parameters,env);});
}
void math() {
  const wz3::AnimationVector p{-.25f,.25f,.5f,1.25f};
  check(eval(unary(0x10,p))==wz3::AnimationVector{1.25f,.75f,.5f,-.25f},"invert lanes");
  check(eval(unary(0x11,p))==wz3::AnimationVector{.25f,-.25f,-.5f,-1.25f},"negate lanes");
  for(unsigned c=0;c<4;++c)check(eval(unary(7+c,p))==wz3::AnimationVector{p[c],p[c],p[c],p[c]},"swizzle broadcasts");
  near(eval(unary(0x12,p))[0],-1,"sine uses turns and broadcasts X");
  near(eval(unary(0x13,{.5f,0,0,0}))[3],-1,"cosine uses turns");
  near(eval(unary(0x15,p))[0],.75f,"negative ramp wraps positive");
  near(eval(unary(0x14,p))[0],0,"pulse negative phase");
  near(eval(unary(0x1c,{0,0,0,0}))[0],114/255.0f,"seed-one noise permutation golden");
  const auto ease=eval(unary(0x1d,{.125f,.25f,.75f,.875f},{f(.25f),f(.25f)}));
  for(unsigned i=0;i<4;++i)near(ease[i],wz3::AnimationVector{1/24.0f,1/6.0f,5/6.0f,23/24.0f}[i],"quadratic ease segments");
  check(eval(unary(0x1e,{0,.25f,.5f,1},{f(64.9f),f(192.9f)}))==wz3::AnimationVector{0,0,.5f,1},"timeshift byte quantization and clamping");
  near(eval(unary(0x20,{2,2,2,2}))[0],.4804530139f,"historical log2 formula");
  check(eval(unary(0x21,{0,1,2,-1}))==wz3::AnimationVector{1,2,4,.5f},"pow2");
  wz3::AnimationEnvironment env;env.variables[28]={2,0,0,0};env.variables[29]={0,3,0,0};env.variables[30]={0,0,4,0};env.variables[31]={5,6,7,1};
  check(eval(unary(0x1b,{1,2,3,1}),env)==wz3::AnimationVector{7,12,19,1},"four-dimensional matrix multiplication");
  for(unsigned cmd:{0x0bu,0x0cu,0x0du,0x0eu,0x0fu,0x1fu}) {
    wz3::Operator op;op.parameters.resize(4);op.animations={node(0,0,3,0x18,0,{f(3)}),node(3,0,3,0x18,0,{f(2)}),node(0,1,6,cmd),node(0,2,6,0x9f)};
    near(eval(op)[0],cmd==0x0b ? 5 : cmd==0x0c ? 1 : cmd==0x0d ? 6 : cmd==0x0e ? 1.5f : cmd==0x0f ? 1 : 9,"binary arithmetic");
  }
  const auto old=std::fegetround();std::fesetround(FE_UPWARD);
  near(eval(unary(0x15,p))[0],.75f,"animation under nondefault caller rounding");
  check(std::fegetround()==FE_UPWARD,"animation must restore caller rounding");std::fesetround(old);
}
void curves_and_events() {
  wz3::Project p;wz3::Spline curve;curve.name="curve";curve.interpolation=0;
  curve.channels={{{f(0),f(0)},{f(1),f(2)},{f(3),f(4)}}};p.splines.push_back(curve);
  curve.name="step";curve.interpolation=2;curve.channels={{{f(0),f(1)},{f(.5f),f(2)},{f(.5f),f(3)},{f(1),f(4)}}};p.splines.push_back(curve);
  curve.name="curve";curve.channels={{{0,f(99)}}};p.splines.push_back(curve);
  const wz3::SplineLibrary splines(p);check(splines.find("curve")==0,"first duplicate spline name wins");
  near(splines.evaluate(0,.5f)[0],13/12.0f,"nonuniform cubic first tangent");near(splines.evaluate(0,2)[0],37/12.0f,"nonuniform cubic last tangent");
  check(splines.evaluate(0,-1)==wz3::AnimationVector{0,0,0,0} && splines.evaluate(0,4)[0]==4,"spline endpoint clamp and missing channels");
  near(splines.evaluate(1,.499f)[0],1,"step left limit");near(splines.evaluate(1,.5f)[0],3,"last duplicate interior key wins");near(splines.evaluate(1,1)[0],4,"step final endpoint");
  rejects([&]{splines.evaluate(0,std::numeric_limits<float>::quiet_NaN());});
  wz3::Operator op;op.parameters.resize(4);op.animations={node(0,0,3,2),node(0,1,3,0x19,0,{},0,"curve"),node(0,2,3,0x9f)};
  auto env=wz3::frame_environment(32768);
  {const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env);near(wz3::as_float(value.parameters[0]),13/12.0f,"named spline bytecode");}
  op.animations[1]=node(0,1,3,0x1a);
  {const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env);check(value.parameters==std::vector<std::uint32_t>(4),"missing event spline must return zero");}
  env.event_spline=0;
  wz3::Event event;event.fields[1]=4*65536;event.fields[2]=8*65536;event.fields[3]=7;event.fields[4]=f(.75f);event.fields[5]=f(.25f);event.fields[9]=0x80402010;
  event.interval={f(.25f),f(.75f)};event.transform={f(1),f(2),f(3),0,0,0,f(4),f(5),f(6)};event.spline="step";
  check(!wz3::event_active(event,4*65536-1) && wz3::event_active(event,4*65536) && !wz3::event_active(event,8*65536),"event interval is half-open");
  const auto before=env.variables;
  {
    const wz3::EventAnimationScope scope(event,6*65536,splines,env);
    near(scope.time,.5f,"event interval remapping");near(env.variables[1][0],.75f,"event velocity");near(env.variables[3][0],7,"event selection is saved as integer");
    check(env.variables[4]==wz3::AnimationVector{1,2,3,1} && env.variables[5][3]==1 && env.variables[6]==wz3::AnimationVector{4,5,6,1},"event transform variables retain Read3 W lane");
    near(env.variables[7][0],64/255.0f,"event color is RGBA, unlike bytecode color constants");check(env.event_spline==1,"event spline binding");
    const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env);near(wz3::as_float(value.parameters[0]),3,"event spline evaluation");
  }
  check(env.variables==before && env.event_spline==0,"event scope must restore variables and spline");
  event.flags=1;event.spline="missing";
  {const wz3::EventAnimationScope scope(event,4*65536,splines,env);check(env.variables[0]==before[0] && env.event_spline==0,"NOTIME and inherited event spline");}
  event.fields[2]=event.fields[1];check(wz3::event_active(event,0),"zero-length event remains active");
  event.fields[10]=1;check(!wz3::event_active(event,0),"disabled event");
}
void theta(const char* file) {
  const auto p=wz3::read_project(file);const auto graph=wz3::connect(p);const wz3::SplineLibrary splines(p);
  unsigned programs=0,reachable=0,nodes=0,used=0;
  for(std::size_t i=0;i<p.operators.size();++i)if(!p.operators[i].animations.empty()) {
    const auto& op=p.operators[i];const auto program=wz3::compile_animation(op,splines);
    ++programs;reachable+=graph.nodes[i].reachable;nodes+=program.source_nodes;used+=program.used_nodes;
    for(unsigned time=0;time<=16;++time) {
      auto env=wz3::frame_environment(time*4096);const auto original=env.variables;
      {const wz3::AnimationScope result(program,splines,op.parameters,env);check(result.parameters.size()==op.parameters.size(),"Theta parameter count");}
      check(env.variables==original,"Theta program leaked scoped variables");
    }
  }
  check(programs==141 && reachable==133 && nodes==1044 && used==981,"Theta animation coverage changed");
  for(std::size_t i=0;i<splines.size();++i)for(unsigned t=0;t<=256;++t)for(auto v:splines.evaluate(i,t/256.0f))check(std::isfinite(v),"Theta nonfinite spline");
  // A real saved material scroll expression, driven by its global beat variable.
  const auto& op=p.operators[348];auto env=wz3::frame_environment(0);env.variables[15].fill(8);
  const wz3::AnimationScope value(wz3::compile_animation(op,splines),splines,op.parameters,env);
  for(unsigned i=40;i<43;++i)near(wz3::as_float(value.parameters[i]),-.8f,"Theta material scroll expression");
  const auto& camera=p.operators[590];env.variables[0].fill(.5f);
  const wz3::AnimationScope pose(wz3::compile_animation(camera,splines),splines,camera.parameters,env);
  const float expected[]={.0900000073f,.0790000036f,0,-5.252972126f,2.3540000916f,-9.3050251007f};
  for(unsigned i=0;i<6;++i)near(wz3::as_float(pose.parameters[3+i]),expected[i],"Theta camera midpoint from saved two-key splines");
}
void release_spline() {
  wz3::Project p;p.runtime_graph=true;wz3::Spline curve;
  curve.channels={{{f(0),f(.25f)},{f(.2f),f(7.5f)},{f(.7f),f(-3.25f)},{f(1),f(4.75f)}}};
  p.splines.push_back(curve);const wz3::SplineLibrary splines(p);
  // Original x86 KSpline::Eval/sHermite outputs, including reflected endpoints
  // and nonuniform interior tangents. The newer Horner kernel differs here.
  for(const auto& [time,bits]:std::vector<std::pair<float,std::uint32_t>>{
      {-.1f,0x3e800000},{0,0x3e800000},{.125f,0x40bfac00},{.375f,0x4087a09c},
      {.625f,0xc01d57bf},{.875f,0x3f0085f0},{1,0x40980000},{1.1f,0x40980000}}) {
    const auto value=splines.evaluate(0,time);
    check(f(value[0])==bits,"released Hermite differs from original x86 instructions");
    check(value[1]==0 && value[2]==0 && value[3]==0,"release spline missing channels");
  }
}
void release_trace(const char* project_file,const char* output_file) {
  const auto p=wz3::read_project(project_file);check(p.runtime_graph,"release trace requires a runtime graph");
  const wz3::SplineLibrary splines(p);check(splines.size()>2,"release trace requires the gait spline");
  std::ofstream out(output_file);check(bool(out),"cannot open release animation trace");out<<"theta-animation-v1\n";
  unsigned programs=0,rows=0;
  for(std::size_t id=0;id<p.operators.size();++id) {
    const auto& op=p.operators[id];if(!op.has_animation())continue;
    const auto program=wz3::compile_animation(op,splines);++programs;
    for(unsigned sample=0;sample<=65;++sample)for(int event_spline:{-1,2}) {
      auto env=wz3::frame_environment(sample<65 ? sample*1024 : 0);
      env.variables[15].fill(sample<65 ? sample*9.25f : 82.825531005859375f);
      // One extra input is the exact camera/event phase at the 36-second capture.
      if(sample==65)env.variables[0].fill((82.825531005859375f-80)/32);
      if(event_spline>=0) {
        env.event_spline=event_spline;env.variables[3].fill(3);
        env.variables[4]={1,2,3,1};env.variables[6]={-2,.5f,3,1};
        for(unsigned foot=0;foot<4;++foot)env.variables[16+foot]={float(foot)-1.5f,.25f*(foot+1),foot&1 ? 2.f : -2.f,1};
      }
      const auto before=env.variables;
      {
        const wz3::AnimationScope value(program,splines,op.parameters,env);
        out<<id<<' '<<sample<<' '<<event_spline<<' '<<value.changed<<' '<<value.parameters.size();
        for(auto word:value.parameters)out<<' '<<word;
        for(const auto& v:env.variables)for(auto lane:v)out<<' '<<std::bit_cast<std::uint32_t>(lane);
        out<<'\n';++rows;
      }
      check(env.variables==before,"release animation leaked scoped variables");
    }
  }
  check(programs==127 && rows==16764,"unexpected release animation coverage");
  out.close();check(bool(out),"cannot finish release animation trace");
  std::cout<<programs<<" released programs, "<<rows<<" evaluations and scope restorations recorded\n";
}
}
int main(int argc,char** argv) {
  try {
    if(argc==4 && std::string_view(argv[1])=="--release-trace") {release_trace(argv[2],argv[3]);return 0;}
    check(argc==2,"expected Theta project, or --release-trace PROJECT OUTPUT");compiler();scope_and_storage();math();curves_and_events();release_spline();theta(argv[1]);
    std::cout<<"animation compilation, scoped execution, events and all 141 Theta programs passed\n";
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
