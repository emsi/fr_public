// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "spline_reference.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
namespace {
void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
void near(float a,float b,const char* reason) {check(std::abs(a-b)<.0001f,reason);}
void equal(const wz3::SplinePose& a,const wz3::SplinePose& b) {
  for(unsigned i=0;i<16;++i)near(a.world[i],b.world[i],"spline transform differs from archived shake");
  near(a.zoom,b.zoom,"spline zoom differs");
}
std::uint32_t f(float v) {return std::bit_cast<std::uint32_t>(v);}
template<class F> void rejects(F action) {bool rejected=false;try {action();}catch(const std::runtime_error&) {rejected=true;}check(rejected,"invalid spatial spline accepted");}
struct Fixture {
  wz3::Project p;wz3::Graph g;
  std::size_t add(unsigned cmd,std::vector<std::uint32_t> params={},std::vector<std::size_t> inputs={},std::string name={}) {
    wz3::Operator op;op.command=cmd;op.parameters=std::move(params);op.name=name;p.operators.push_back(op);g.nodes.push_back({true,true,std::move(inputs),{}});
    const auto id=p.operators.size()-1;if(!name.empty())g.names[name]=id;return id;
  }
  void blob(std::size_t id,const std::vector<std::uint32_t>& words) {
    auto& op=p.operators[id];op.blob_offset=p.bytes.size();op.blob_size=words.size()*4;
    for(auto word:words)for(unsigned i=0;i<4;++i)p.bytes.push_back(std::uint8_t(word>>(8*i)));
  }
  std::size_t curve(unsigned mode=0,float zoom=1) {
    const auto id=add(0x18);std::vector<std::uint32_t> b(16);b[0]=2;b[1]=3;b[3]=mode;b[6]=f(10);b[7]=f(1);
    b.insert(b.end(),{0,0,0,0,0,0,0,0,f(zoom),0,f(1),f(10),0,0,0,0,0,f(zoom)});blob(id,b);return id;
  }
  void event(std::string name,int start,int end) {
    wz3::Event e;e.name=std::move(name);e.fields[1]=std::bit_cast<unsigned>(start);e.fields[2]=std::bit_cast<unsigned>(end);p.events.push_back(e);
  }
};
void paths() {
  for(unsigned mode:{0u,1u,2u,3u,5u}) {
    Fixture fx;const auto id=fx.curve(mode,2);wz3::SpatialSplines splines(fx.p,fx.g);
    for(float t:{0.f,.125f,.5f,.9f,1.f,2.f}) {
      const auto pose=splines.evaluate(id,t,0,wz3::frame_environment(0));near(pose.world[12],10*std::clamp(t,0.f,1.f),"two-key Hermite position");near(pose.zoom,2,"spline zoom");
      if(mode==3)near(pose.world[8],1,"derivative-facing mode must look along X");
    }
  }
  Fixture fx;const auto source=fx.curve(0,2),scaled=fx.add(0x1c,{f(2),f(1),f(1),5},{source});
  const auto scene=fx.add(0xc1),view=fx.add(0xf0,{3,3,0xff000000,0,0,0,f(999),0,0,f(4096),f(.125f),0,0,f(2),f(3),0xff000000,f(4096),f(16)},{scene,scaled});
  wz3::ScenePlayer player(fx.p,fx.g);const auto frame=player.viewport(view,wz3::frame_environment(32768),2);
  near(frame.camera.world[12],10,"viewport spline must replace saved camera translation");near(frame.camera.zoom_x,4,"camera horizontal zoom multiplier");near(frame.camera.zoom_y,12,"camera aspect and spline zoom");
  near(frame.lights[0].position[0],10,"fallback light must follow spline camera");
  wz3::SpatialSplines splines(fx.p,fx.g);const auto expected=splines.evaluate(source,.37f,0,wz3::frame_environment(0));
  std::fesetround(FE_DOWNWARD);equal(splines.evaluate(source,.37f,0,wz3::frame_environment(0)),expected);check(std::fegetround()==FE_DOWNWARD,"spline leaked rounding mode");std::fesetround(FE_TONEAREST);
  const auto pipe=fx.add(0x1a);std::vector<std::uint32_t> b(24);b[0]=1;b[1]=1;b[18]=f(10);fx.blob(pipe,b);
  wz3::SpatialSplines pipes(fx.p,fx.g);
  for(float t:{0.f,.5f,1.f}) {const auto pose=pipes.evaluate(pipe,t,0,wz3::frame_environment(0));near(pose.world[14],t*10,"straight pipe position");near(pose.zoom,1,"pipe quaternion must not become zoom");}
}
void shake() {
  for(unsigned mode:{0u,1u,2u,3u,16u,17u,18u,19u,32u,33u,34u,35u,48u,49u,50u,51u}) {
    Fixture fx;const auto parent=fx.curve(),amp=fx.curve(0,.75f);std::vector<std::uint32_t> p(17);p[0]=mode;
    for(unsigned i=1;i<13;++i)p[i]=f(i*.17f);p[14]=f(2);p[15]=f(-3);p[16]=f(.5f);
    const auto id=fx.add(0x19,p,{parent,amp},"shake");fx.event("shake",0,65536);fx.event("shake",1000,3*65536);fx.event("shake",0,0);
    wz3::SpatialSplines splines(fx.p,fx.g);wz3::EventTimeline timeline(fx.p,fx.g);
    for(float time:{0.f,.17f,.5f,.93f,1.f}) {
      auto env=wz3::frame_environment(0);env.variables[0][0]=time;
      const auto base=splines.evaluate(parent,time,0,env);
      equal(splines.evaluate(id,time,0,env),reference::shake(p,base,.75f,time));
      timeline.begin_frame(32768);const std::array<std::array<std::int32_t,2>,3> events{{{0,0},{1000,3*65536},{0,65536}}};
      equal(splines.evaluate(id,time,0,env,&timeline),reference::shake(p,base,.75f,time,events,32768));
    }
  }
}
void apply_to_scene() {
  Fixture fx;const auto curve=fx.curve(0,7),marker=fx.add(0x182,{2});
  const auto applied=fx.add(0x181,{f(.25f)},{marker,curve});
  wz3::Animation load,store;load.fields={0,0,3,2,0,0,0,0};store.fields={0,1,3,0x91,0,5,0,0};fx.p.operators[applied].animations={load,store};
  const auto parent=fx.add(0xc3,{f(2),f(1),f(1),0,0,0,f(3),0,0},{applied});
  const auto view=fx.add(0xf0,{3,3,0xff000000,0,0,0,0,0,f(-5),f(4096),f(.125f),0,0,f(1),f(1),0xff000000,f(4096),f(16)},{parent});
  wz3::ScenePlayer player(fx.p,fx.g);
  for(int beat:{0,16384,32768,65536}) {
    const auto frame=player.viewport(view,wz3::frame_environment(beat),1);
    near(player.markers()[2][12],3+20*(beat/65536.f),"Apply Spline must evaluate its animated time and compose with parent");
    near(frame.camera.zoom_x,1,"scene spline zoom must not change camera zoom");
    near(frame.camera.world[12],0,"scene spline must not replace camera");
  }
}
void invalid() {
  for(unsigned kind=0;kind<7;++kind) {
    Fixture fx;const auto id=fx.curve();auto& op=fx.p.operators[id];
    if(kind==0)op.blob_size=63;
    if(kind==1)op.blob_size-=1;
    if(kind==2)fx.p.bytes[0]=1;
    if(kind==3)fx.p.bytes[4]=4;
    if(kind==4)fx.p.bytes[12]=6;
    if(kind==5) {fx.p.bytes[108]=0;fx.p.bytes[109]=0;fx.p.bytes[110]=0xc0;fx.p.bytes[111]=0x7f;}
    if(kind==6)op.blob_offset=fx.p.bytes.size();
    wz3::SpatialSplines splines(fx.p,fx.g);rejects([&]{splines.evaluate(id,.5f,0,wz3::frame_environment(0));});
  }
  Fixture fx;const auto id=fx.add(3);fx.g.nodes[id].inputs={id};wz3::SpatialSplines splines(fx.p,fx.g);rejects([&]{splines.evaluate(id,0,0,wz3::frame_environment(0));});
}
void debris(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::SpatialSplines splines(p,g);unsigned count=0,samples=0;std::array<unsigned,5> kinds{};
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && wz3::operator_info(p.operators[id].command).category=="SPLINE") {
    try {
      auto env=wz3::frame_environment(0);splines.precalculate(id,env);++count;++kinds.at(p.operators[id].command-0x18);
      for(unsigned i=0;i<=100;++i) {env.variables[0][0]=i/100.f;splines.evaluate(id,i/100.f,0,env);++samples;}
    }catch(const std::exception& e) {throw std::runtime_error("spline "+std::to_string(id)+": "+e.what());}
  }
  check(kinds[0]==133 && kinds[2]==31 && kinds[4]==2,"Debris spatial spline inventory changed");
  std::cout<<count<<" Debris spatial splines, "<<samples<<" poses; kinds ";for(auto k:kinds)std::cout<<k<<' ';std::cout<<'\n';
}
}
int main(int argc,char** argv) {
  try {check(argc==2,"expected Debris project");paths();shake();apply_to_scene();invalid();debris(argv[1]);}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
