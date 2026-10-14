// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Stateful gait, two-bone IK and axis alignment from genscene.cpp.
#include "wz3/walk.hpp"
#include "mesh_kernel.hpp"
#include <cfenv>
namespace wz3 {
namespace {
using V=legacy::sVector;using M=legacy::sMatrix;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Nearest {int previous=std::fegetround();Nearest(){std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetround(previous);}};
float real(std::span<const std::uint32_t> p,unsigned i) {const auto v=as_float(p[i]);require(std::isfinite(v) && std::abs(v)<=1e6f,"invalid gait numeric parameter");return v;}
int rounded(float value) {const auto v=std::nearbyint(double(value));require(v>=0 && v<=2147483647,"invalid gait duration");return int(v);}
V vector(std::span<const std::uint32_t> p,unsigned i) {V v;v.Init(real(p,i),real(p,i+1),real(p,i+2),0);return v;}
M matrix(const SceneMatrix& m) {M out;out.i.Init(m[0],m[1],m[2],m[3]);out.j.Init(m[4],m[5],m[6],m[7]);out.k.Init(m[8],m[9],m[10],m[11]);out.l.Init(m[12],m[13],m[14],m[15]);return out;}
SceneMatrix matrix(const M& m) {SceneMatrix out={m.i.x,m.i.y,m.i.z,m.i.w,m.j.x,m.j.y,m.j.z,m.j.w,m.k.x,m.k.y,m.k.z,m.k.w,m.l.x,m.l.y,m.l.z,m.l.w};for(auto v:out)require(std::isfinite(v),"degenerate gait/limb orientation");return out;}
V cross(const V& a,const V& b) {V out;out.Cross3(a,b);out.w=0;return out;}
float length(const V& v) {return std::sqrt(v.Dot3(v));}
void unit(V& v) {require(v.Dot3(v)>0,"degenerate gait/limb direction");v.Unit3();}
}
struct SceneWalk::Impl {
  const SplineLibrary& splines;
  struct Memory {
    std::array<V,8> old_feet{},new_feet{},delta{};
    std::array<int,8> step_time{};
    int last_time{},state{},last_leg{},next_leg{},initial_steps{};
    unsigned count{};bool idle{};
    V center,new_position;
  };
  std::map<std::size_t,Memory> memory;
  explicit Impl(const SplineLibrary& s):splines(s) {}
};
SceneWalk::SceneWalk(const SplineLibrary& s):impl(std::make_unique<Impl>(s)) {}
SceneWalk::~SceneWalk()=default;
WalkPose SceneWalk::evaluate(std::size_t instance,std::span<const std::uint32_t> p,const SceneMatrix& world,const FrameClock& clock,std::optional<std::size_t> step_spline) {
  const Nearest rounding;require(p.size()==39 && !(p[0]&~0x1ffu) && p[1]>=1 && p[1]<=8,"unsupported Walk layout/flags/count");
  const auto flags=p[0];const int count=p[1];const int group[2]={int(p[5]),int(p[6])};
  require(group[0]>0 && group[0]<=count && group[1]>0 && group[1]<=count,"invalid Walk foot groups");
  for(unsigned i=2;i<39;++i)if(i!=5 && i!=6)real(p,i);
  require(real(p,2)>=0 && real(p,3)>=0 && real(p,4)>=real(p,3) && real(p,7)>=0 && real(p,8)>=0,"invalid Walk thresholds/length");
  require(clock.current>=0,"negative gait clock");
  const float step_length[2]={real(p,7),real(p,8)};
  const int speed[2]={rounded(real(p,9)*1000),rounded(real(p,10)*1000)};
  require(speed[0]>0 && speed[1]>0,"Walk step duration rounds to zero");
  const int next[2]={std::min(rounded(real(p,11)*1000),speed[0]),std::min(rounded(real(p,12)*1000),speed[1])};
  require(next[0]>0 && next[1]>0,"Walk next-step duration rounds to zero");
  require(impl->memory.contains(instance) || impl->memory.size()<65536,"Walk instance memory limit");
  auto [it,inserted]=impl->memory.try_emplace(instance);auto& mem=it->second;const auto input=matrix(world);
  if(inserted || (flags&0x100) || clock.reset) {
    mem.center.Init();mem.initial_steps=count*2;mem.count=count;
    for(int i=0;i<count;++i) {
      mem.delta[i]=vector(p,13+i*3);auto v=input.l;v.Add3(mem.delta[i]);v.w=0;
      mem.old_feet[i]=mem.new_feet[i]=v;mem.step_time[i]=speed[0];
      mem.center.x+=mem.delta[i].x/count;mem.center.z+=mem.delta[i].z/count;
    }
    mem.last_time=clock.current;mem.new_position=input.l;mem.state=mem.last_leg=0;
    for(int i=0;i<count;++i)mem.delta[i].Sub3(mem.center);
  }
  require(mem.count==unsigned(count),"animated Walk foot count needs a reset");
  auto state=mem.state,last_leg=mem.last_leg,next_leg=mem.next_leg;
  // Editor cycle helpers are active for .k projects (the non-sINTRO path).
  if(flags&0x30) {
    state=(flags&0x30)==0x20;const auto j=group[state];float f=(real(p,3)+real(p,4))*.5f/j;if(flags&0x40)f*=2;
    const auto cycle=std::int64_t(next[state])*j;require(cycle<=2147483647,"Walk cycle duration overflow");
    const int time=clock.current%int(cycle),k=clock.current/int(cycle);
    for(int i=0;i<count;++i) {
      mem.step_time[i]=time-next[state]*(i%j);
      if(flags&0x80)mem.old_feet[i].Init(0,0,f*(i%j-float(time)/next[state]-(j-1)*.5f));
      else mem.old_feet[i].Init(0,0,f*(k*j+i%j));
      mem.old_feet[i].Add3(mem.delta[i]);mem.new_feet[i]=mem.old_feet[i];mem.new_feet[i].z+=f*j;
      if(mem.step_time[i]<speed[state]-next[state]*j) {mem.step_time[i]+=next[state]*j;mem.old_feet[i].z-=f*j;mem.new_feet[i].z-=f*j;}
      if(mem.step_time[i]<0) {mem.new_feet[i]=mem.old_feet[i];mem.step_time[i]=speed[state];}
      mem.step_time[i]=std::min(mem.step_time[i],speed[state]);
    }
  }
  std::array<V,8> feet,splines;V sum;sum.Init();float phase_sum=0;
  for(int i=0;i<count;++i) {
    const float t=std::clamp(float(mem.step_time[i])/speed[state],0.f,1.f);if(i<group[state])phase_sum+=t;
    if(step_spline) {const auto v=impl->splines.evaluate(*step_spline,(t+state)*.5f);splines[i].Init(v[0],v[1],v[2],v[3]);if(mem.delta[i].x<0)splines[i].x=-splines[i].x;if(mem.delta[i].z<0)splines[i].z=-splines[i].z;splines[i].w-=state;}
    else splines[i].Init(0,legacy::sFSin(t*legacy::sPI)*.25f,0,t);
    feet[i].Lin3(mem.old_feet[i],mem.new_feet[i],splines[i].w);feet[i].w=1;sum.Add3(feet[i]);
  }
  sum.Scale3(1.f/count);M body;body.i.Init(0,0,0,0);body.j.Init(0,0,0,0);body.k.Init(0,0,0,0);body.l.Init(sum.x,sum.y,sum.z,1);
  for(int i=0;i<count;++i) {V v;v.Sub3(feet[i],sum);body.i.AddScale3(v,mem.delta[i].x);body.j.AddScale3(v,mem.delta[i].y);body.k.AddScale3(v,mem.delta[i].z);}
  switch((flags>>2)&3) {
  case 0:body.i=input.i;body.j=input.j;body.k=input.k;break;
  case 2:unit(body.i);body.j.Init(0,1,0,0);body.k=cross(body.i,body.j);unit(body.k);body.j=cross(body.k,body.i);unit(body.j);break;
  case 1:body.k=input.k;[[fallthrough]];
  case 3:unit(body.k);body.j=cross(body.k,body.i);unit(body.j);body.i=cross(body.j,body.k);unit(body.i);break;
  }
  auto phase=(phase_sum+last_leg)/group[state];if(phase>=2)phase-=2;if(phase>=1)phase-=1;
  WalkPose pose;pose.body=matrix(body);pose.count=count;pose.timing={(phase+state)*.5f,0,0,0};
  // Source computes the visible pose before changing its scheduler/state.
  const int elapsed=std::max(0,clock.current-mem.last_time);mem.last_time=clock.current;
  auto dir=input;dir.k.y=0;unit(dir.k);dir.j.Init(0,1,0,0);dir.i=cross(dir.j,dir.k);
  for(int i=0;i<count;++i)if(mem.step_time[i]<speed[state]) {const auto time=std::int64_t(mem.step_time[i])+elapsed;require(time<=2147483647,"Walk step clock overflow");mem.step_time[i]=int(time);}
  float distance=0;next_leg=(last_leg+1)%group[state];int best_leg=next_leg;
  for(int i=0;i<count;++i) {
    V v;v.Rotate34(dir,mem.delta[i]);v.Sub3(feet[i]);v.y=0;const auto d=length(v);
    if(d>distance && mem.step_time[i]>=speed[state]) {distance=d;best_leg=i;}
    v.Rotate3(dir,splines[i]);v.Add3(feet[i]);pose.feet[i]={v.x,v.y,v.z,splines[i].w};
  }
  int change=1;for(int i=0;i<count;++i)if(mem.step_time[i]<speed[state])change=0;
  const bool switch_state=(state==0 && distance>real(p,4)) || (state==1 && distance<real(p,3));
  if(switch_state)change+=2;
  if(change==3 && switch_state) {state=1-state;change=0;last_leg%=group[state];for(int i=0;i<count;++i)mem.step_time[i]=speed[state];next_leg=best_leg%group[state];}
  best_leg%=group[state];if(mem.idle && change==1)next_leg=best_leg;
  mem.idle=false;
  if(change!=2 && mem.step_time[last_leg]>=next[state] && mem.step_time[next_leg]>=speed[state]) {
    if(mem.initial_steps>0) {--mem.initial_steps;distance=1024;}
    if(distance>real(p,2)) {
      V v;v.Sub3(dir.l,body.l);const auto d=length(v);if((flags&1) && !mem.initial_steps)next_leg=best_leg;
      if(d>step_length[state]) {mem.new_position=body.l;mem.new_position.AddScale3(v,step_length[state]/d);}else mem.new_position=dir.l;
      dir.l=mem.new_position;const int time=mem.step_time[last_leg]-next[state];
      for(int i=next_leg;i<count;i+=group[state]) {mem.old_feet[i]=feet[i];v=mem.delta[i];v.y=0;mem.new_feet[i].Rotate34(dir,v);mem.step_time[i]=time;}
      last_leg=next_leg;
    }else mem.idle=true;
  }
  mem.state=state;mem.last_leg=last_leg;mem.next_leg=next_leg;return pose;
}
LimbPose scene_limb(std::span<const std::uint32_t> p,const SceneMatrix& world) {
  const Nearest rounding;require(p.size()==9 && !(p[8]&~3u),"unsupported Limb layout/flags");
  auto position=vector(p,0),direction=vector(p,3);const auto a=real(p,6),b=real(p,7);const auto input=matrix(world);const auto flags=p[8];
  if(!(flags&2))position.Sub3(input.l);
  V j,i,k;j.Init(-position.x,-position.y,-position.z,0);i=direction;if(!(flags&2))i.Rotate3(input);
  k=cross(i,j);i=cross(j,k);const auto c=length(j);unit(i);unit(j);unit(k);
  const auto t=(c*c+a*a-b*b)/(2*c);auto s=(a-t)*(a+t);s=s<=0 ? 0 : std::sqrt(s);
  M upper,lower;upper.k=k;upper.j.Scale4(j,t);upper.j.AddScale3(i,s);unit(upper.j);upper.i=cross(upper.j,upper.k);upper.l.Init(0,0,0,1);
  if(a<=t && !(flags&1)) {lower=upper;lower.l.AddScale3(j,-a-b);}
  else {lower.k=k;lower.j.Scale4(j,c-t);lower.j.AddScale3(i,-s);unit(lower.j);lower.i=cross(lower.j,lower.k);lower.l.Init(position.x,position.y,position.z,1);}
  const auto u=matrix(upper),l=matrix(lower);LimbPose result;result.other={l,u};
  if(flags&2)result.world={scene_combine(world,u),scene_combine(world,l)};
  else {upper.l.Add3(input.l);lower.l.Add3(input.l);result.world={matrix(upper),matrix(lower)};}
  return result;
}
SceneMatrix scene_rotate(std::span<const std::uint32_t> p,const SceneMatrix& world,const SceneCamera& camera) {
  const Nearest rounding;require(p.size()==4 && p[3]<=3,"unsupported Rotate layout/axis");
  auto v=vector(p,0);v.UnitSafe3();const auto input=matrix(world);M out;
  switch(p[3]) {
  case 0:out.i=v;out.k=cross(out.i,input.j);unit(out.k);out.j=cross(out.k,out.i);unit(out.j);break;
  case 1:out.j=v;out.i=cross(out.j,input.k);unit(out.i);out.k=cross(out.i,out.j);unit(out.k);break;
  case 2:out.k=v;out.j=cross(out.k,input.i);unit(out.j);out.i=cross(out.j,out.k);unit(out.i);break;
  default:out=matrix(camera.world);break;
  }
  out.l=input.l;return matrix(out);
}
} // namespace wz3
