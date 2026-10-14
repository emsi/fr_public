// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/effect_particles.hpp"
#include "mesh_kernel.hpp"
#include <cstring>
#include <iostream>
#include <emmintrin.h>

namespace source {
using sInt=int;using sU32=std::uint32_t;using sU64=std::uint64_t;using sF32=float;
using sVector=wz3::legacy::sVector;using sF323=sVector;
struct sMatrix:wz3::legacy::sMatrix {
  void Trans3() {std::swap(i.y,j.x);std::swap(i.z,k.x);std::swap(j.z,k.y);}
};
int sMin(int a,int b) {return std::min(a,b);}
float sFade(float a,float b,float v) {return a+(b-a)*v;}
int sDivShift(int a,int b) {check(b>0,"oracle divide by zero");return std::int64_t(a)*65536/b;}
void sFSinCos(float v,float& s,float& c) {s=std::sin(double(v));c=std::cos(double(v));}
// SSE2 translation of the archived MMX primitive, independent of native fade64.
void Fade64(sU64& out,sU64& a,sU64& b,int fade) {
  auto weights=_mm_set1_epi32(fade);weights=_mm_srai_epi32(weights,1);
  weights=_mm_xor_si128(weights,_mm_set_epi32(-1,0,-1,0));
  weights=_mm_add_epi32(weights,_mm_set_epi32(32769,0,32769,0));
  weights=_mm_packs_epi32(weights,weights);
  const auto w1=_mm_shufflelo_epi16(weights,0),w0=_mm_shufflelo_epi16(weights,0x55);
  const auto av=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(a)),bv=_mm_cvtsi64_si128(std::bit_cast<std::int64_t>(b));
  out=std::bit_cast<sU64>(_mm_cvtsi128_si64(_mm_slli_epi16(_mm_add_epi16(_mm_mulhi_epi16(av,w0),_mm_mulhi_epi16(bv,w1)),1)));
}
struct Camera {sMatrix ModelSpace;};
struct Shader {void Set(Camera&) {}} shader;
struct GenMaterial {struct Pass {Shader* Mtrl=&shader;};struct List {int Count=1;Pass value;Pass& operator[](int){return value;}} Passes;};
struct KOp {GenMaterial* material;void* GetLinkCache(int) {return material;}};
struct KEnvironment {int TimeDelta{};Camera CurrentCam;struct Stack {sMatrix matrix;const sMatrix& Top(){return matrix;}} ExecStack;};
constexpr unsigned sFVF_TSPACE3=1,sGEO_QUAD=1,sGT_MODELVIEW=1,MAXPARTSLOT=16;
struct System {
  sMatrix view;std::vector<float> buffer,output;
  int GeoAdd(int,int) {output.clear();return 1;}
  void GetTransform(int,sMatrix& out) {out=view;}
  void GeoBegin(int,int count,int,float** ptr,void*) {buffer.resize(count*8);*ptr=buffer.data();}
  void GeoEnd(int) {} void GeoDraw(int) {output.insert(output.end(),buffer.begin(),buffer.end());}void GeoRem(int) {}
} system;
System* sSystem=&system;
#define __stdcall
#define sVERIFY(x) check(x,"archived particle assertion")
#include "effect_particles_oracle.inc"
#undef sVERIFY
#undef __stdcall
void compare(wz3::EffectParticles& native,const std::vector<std::uint32_t>& params,const wz3::SceneCamera& camera,const wz3::SceneMatrix& world,wz3::FrameClock clock) {
  std::vector<Particle> storage;
  for(const auto& v:native.particles(params[0])) {Particle p;p.Pos.Init(v.position[0],v.position[1],v.position[2],v.position[3]);p.Speed.Init(v.speed[0],v.speed[1],v.speed[2],v.speed[3]);p.Time=v.age;p.MaxTime=v.lifetime;storage.push_back(p);}
  ParticleSystems[params[0]]={storage.data(),int(storage.size()),int(storage.size())};
  const auto matrix=[](const auto& m){sMatrix out;out.i.Init(m[0],m[1],m[2],m[3]);out.j.Init(m[4],m[5],m[6],m[7]);out.k.Init(m[8],m[9],m[10],m[11]);out.l.Init(m[12],m[13],m[14],m[15]);return out;};
  system.view=matrix(camera.world);system.view.TransR();GenMaterial material;KOp op{&material};KEnvironment env;env.TimeDelta=clock.delta;env.ExecStack.matrix=matrix(world);
  const auto real=[&](unsigned i){return wz3::as_float(params[i]);};sVector size;size.Init(real(5),real(6),real(7));
  Exec_Effect_PartSystem(&op,&env,params[0],params[1],params[2],params[3],params[4],size,real(8),real(9),real(10),params[11]);
  const auto vertices=native.draw(params,camera,world,clock);check(vertices.size()*8==system.output.size(),"source/native billboard count mismatch");
  for(std::size_t i=0;i<vertices.size();++i) {
    const auto& v=vertices[i];const auto* expected=system.output.data()+i*8;
    for(unsigned c=0;c<3;++c)check(std::abs(v.position[c]-expected[c])<.0001f,"source/native billboard position mismatch");
    std::uint32_t color;std::memcpy(&color,expected+5,4);check(v.color==color,"source/native particle color mismatch");
    check(v.uv[0]==expected[6] && v.uv[1]==expected[7],"source/native particle UV mismatch");
  }
  const auto state=native.particles(params[0]);check(state.size()==std::size_t(ParticleSystems[params[0]].Used),"source/native surviving particle count mismatch");
  for(std::size_t i=0;i<state.size();++i) {
    check(state[i].age==storage[i].Time && state[i].lifetime==storage[i].MaxTime,"source/native particle age mismatch");
    for(unsigned c=0;c<4;++c) {near(state[i].position[c],storage[i].Pos[c],"source/native simulation position");near(state[i].speed[c],storage[i].Speed[c],"source/native simulation speed");}
  }
}
}
namespace {
std::vector<std::uint32_t> emitter(unsigned slot=0) {return {slot,0,0,f(1),f(100),f(2),f(3),f(4),f(.2f),0,0,0,0,0,0,0,0};}
std::vector<std::uint32_t> system(unsigned slot=0) {return {slot,64,0xff2080e0,0x80503070,0x10203040,f(1),f(2),f(3),f(.4f),f(.2f),f(1),0};}
void clocks_and_emission() {
  wz3::FrameClock clock;clock.advance(25);check(clock.current==25 && clock.delta==25 && !clock.reset,"first clock interval");
  wz3::EffectParticles particles;particles.initialize(0,3);auto emit=emitter();auto world=wz3::scene_identity();world[12]=10;
  particles.emit(0,emit,world,clock);check(particles.particles(0).size()==2,"emission must truncate accumulated rate");
  near(particles.particles(0)[0].position[0],10,"world emitter position");near(particles.particles(0)[0].speed[0],.002f,"speed per millisecond");
  const auto first=particles.draw(system(),{},world,clock);check(first.size()==8 && particles.particles(0)[0].age==25,"draw then advance");
  near((first[0].position[0]+first[2].position[0])*.5f,10,"draw used future position");
  clock.advance(50);particles.emit(0,emit,world,clock);check(particles.particles(0).size()==3,"slot capacity");
  clock.advance(10);check(clock.reset && clock.delta==0,"backward clock reset");particles.draw(system(),{},world,clock);check(particles.particles(0)[0].age==25,"backward clock must retain particle state");
  clock.advance(1010);particles.draw(system(),{},world,clock);check(particles.particles(0).size()==1,"swap-delete expired particles");
  clock.advance(1011);particles.draw(system(),{},world,clock);check(particles.particles(0).empty(),"strict greater-than lifetime expiration");
  check(wz3::beat_milliseconds(138*65536,138)==60000,"saved BPM conversion");
  wz3::EffectParticles burst;burst.initialize(0,64);emit=emitter();emit[2]=1;clock={10,10,false};
  burst.emit(0,emit,world,clock);burst.emit(0,emit,world,clock);burst.emit(1,emit,world,clock);check(burst.particles(0).size()==2,"burst memory must be per instance");
  emit[1]=0x100;burst.emit(0,emit,world,clock);emit[1]=0;burst.emit(0,emit,world,clock);check(burst.particles(0).size()==3,"disabled emitter rearms its burst");
  wz3::EffectParticles slots;slots.initialize(0,4);slots.initialize(1,4);emit=emitter();emit.resize(20);emit[17]=2;clock={40,40,false};slots.emit(0,emit,world,clock);
  check(slots.particles(0).size()==2 && slots.particles(1).size()==2,"round-robin slots");
}
void oracle() {
  for(unsigned flags=0;flags<2;++flags)for(float fade:{0.f,.2f,.5f})for(float middle:{0.f,.4f,1.f}) {
    wz3::EffectParticles particles;particles.initialize(0,64);auto e=emitter();e[3]=f(.2f);for(unsigned i=9;i<17;++i)e[i]=f(.2f);
    auto p=system();p[8]=f(middle);p[9]=f(fade);p[11]=flags;
    auto world=wz3::scene_srt(std::array<std::uint32_t,9>{f(2),f(3),f(1),f(.1f),f(.2f),f(.3f),f(1),f(2),f(3)});
    wz3::SceneCamera camera;camera.world=wz3::scene_srt(std::array<std::uint32_t,9>{f(1),f(1),f(1),f(.2f),f(.1f),f(.4f),0,0,0});
    wz3::FrameClock clock;for(int t:{10,25,76,130,210,300,500}) {clock.advance(t);particles.emit(0,e,world,clock);source::compare(particles,p,camera,world,clock);}
  }
}
void deferred_events() {
  FrameFixture x;const auto material=x.material(0xffffffff,0x2200);const auto a=x.add(0x64,system(),{},{material});
  auto e=emitter();e[2]=1;const auto emit=x.add(0x63,e);
  const auto first=x.event(emit,"one",0,4*65536),second=x.event(emit,"two",0,4*65536);
  const auto b=x.add(0x64,system(),{},{material}),scene=x.add(0xc1,{}, {a,first,second,b}),vp=x.viewport();x.g.nodes[vp].inputs={scene};
  const auto root=x.add(0x0d,{}, {vp});wz3::FramePlayer player(x.p,x.g);
  const auto frame=player.frame(root,0,32,32,10);const auto& s=std::get<wz3::SceneFrame>(frame.commands[0].draw);
  check(s.effects.size()==2 && s.effects[0].operator_id==b && s.effects[1].operator_id==a,"equal-pass effects must paint in reverse traversal order");
  check(s.effects[0].vertices.size()==8,"deferred paint must see both event emitters");
  near((s.effects[0].vertices[0].position[0]+s.effects[0].vertices[2].position[0])*.5f,0,"first effect draws before movement");
  near((s.effects[1].vertices[0].position[0]+s.effects[1].vertices[2].position[0])*.5f,.02f,"second effect advances same slot again");
  const auto next=player.frame(root,65536,32,32,20);check(std::get<wz3::SceneFrame>(next.commands[0].draw).effects[0].vertices.size()==8,"event burst counters must persist");
  const auto later=x.material(0xffffffff,0x2200);x.p.operators[later].parameters[49]=2;x.g.nodes[b].links[0]=later;
  wz3::FramePlayer ordered(x.p,x.g);const auto different=ordered.frame(root,0,32,32,10);const auto& sorted=std::get<wz3::SceneFrame>(different.commands[0].draw);
  check(sorted.effects[0].operator_id==a && sorted.effects[1].operator_id==b && sorted.effects[0].sort_key<sorted.effects[1].sort_key,"effect pass sort must override insertion order");
  x.p.operators[later].parameters[49]=std::bit_cast<std::uint32_t>(-3);
  const auto adjusted=x.add(0x180,{5},{x.add(0xc1,{}, {b})});x.g.nodes[scene].inputs.back()=adjusted;
  wz3::FramePlayer offset(x.p,x.g);const auto adjusted_frame=offset.frame(root,0,32,32,10);
  const auto& draws=std::get<wz3::SceneFrame>(adjusted_frame.commands[0].draw).effects;
  check(draws[1].sort_key>>24==2,"effect pass must add adjustment before clamping signed initial pass");
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::FramePlayer player(p,g);wz3::DemoFrame frame;
  for(int ms=0;ms<=1000;ms+=10)frame=player.frame(g.names.at("root"),std::int32_t(ms*wz3::as_float(p.settings[4])*65536.0/60000),64,48,ms);
  std::size_t particles=0;float maxz=-1e9f;
  for(const auto& cmd:frame.commands)if(const auto* s=std::get_if<wz3::SceneFrame>(&cmd.draw))for(const auto& e:s->effects) {particles+=e.vertices.size()/4;for(const auto& v:e.vertices)maxz=std::max(maxz,v.position[2]);}
  check(particles==100 && maxz>30,"Theta opening emitter must accumulate moving particles over real frame intervals");
  std::cout<<"Theta opening: "<<particles<<" persistent particles after 1000 ms\n";
}
}
int main(int argc,char** argv) {
  try {clocks_and_emission();oracle();deferred_events();if(argc>1)theta(argv[1]);}
  catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
