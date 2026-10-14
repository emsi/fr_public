// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/scene.hpp"
#include <bit>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void near(float a,float b,const char* message) {check(std::abs(a-b)<.0001f,message);}
std::uint32_t f(float value) {return std::bit_cast<std::uint32_t>(value);}
template<class F> void rejects(F fn) {bool caught=false;try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid timeline accepted");}
struct Fixture {
  wz3::Project p;wz3::Graph g;
  std::size_t add(unsigned cmd,std::vector<std::uint32_t> params={},std::vector<std::size_t> inputs={},std::string name={}) {
    const auto id=p.operators.size();wz3::Operator op;op.command=cmd;op.parameters=std::move(params);op.name=name;
    p.operators.push_back(std::move(op));g.nodes.push_back({true,true,std::move(inputs),{}});if(!name.empty())g.names.emplace(name,id);return id;
  }
  std::size_t event(std::string name,int start,int end,int selection=0,float x=0) {
    wz3::Event e;e.name=std::move(name);e.fields[0]=999999; // deliberately stale saved operator index
    e.fields[1]=std::bit_cast<unsigned>(start);e.fields[2]=std::bit_cast<unsigned>(end);e.fields[3]=selection;
    e.fields[4]=e.fields[5]=f(1);e.fields[9]=0xffffffff;e.interval={0,f(1)};e.transform={f(1),f(1),f(1),0,0,0,f(x),0,0};
    p.events.push_back(e);return p.events.size()-1;
  }
  void animate(std::size_t id,unsigned variable,unsigned offset,unsigned mask=1) {
    wz3::Animation load,store;load.fields={0,0,3,2,variable,0,0,0};store.fields={0,1,3,0x90|mask,offset,5,0,0};p.operators[id].animations={load,store};
  }
  std::size_t cube() {return add(0x81,{1,1,1,1,f(1),f(1),f(1),0,0,0,0,0,0});}
  std::size_t transform(std::size_t input) {return add(0xc3,{f(1),f(1),f(1),0,0,0,0,0,0},{input});}
  std::size_t viewport(std::size_t input) {return add(0xf0,{3,3,0xff000000,0,0,0,0,0,f(-5),f(4096),f(.125f),0,0,f(1),f(1),0xff000000,f(4096),f(16)},{input});}
};
void scheduling() {
  Fixture fx;const auto op=fx.add(6,{0},{},"event");fx.event("event",0,65536);fx.event("event",0,2*65536);fx.event("event",65536,3*65536);
  const auto disabled=fx.event("event",0,9*65536);fx.p.events[disabled].fields[10]=1;fx.event("unresolved",0,9*65536);
  wz3::EventTimeline timeline(fx.p,fx.g);rejects([&]{timeline.beat();});rejects([&]{timeline.active(op);});
  timeline.begin_frame(0);check(std::vector<std::size_t>(timeline.active(op).begin(),timeline.active(op).end())==std::vector<std::size_t>{1,0},"events must resolve by name and run in reverse file order");
  timeline.begin_frame(65536);check(std::vector<std::size_t>(timeline.active(op).begin(),timeline.active(op).end())==std::vector<std::size_t>{2,1},"start is inclusive and end exclusive");
  timeline.begin_frame(-1);check(timeline.active(op).empty(),"negative beat should precede positive events");
  timeline.reset();rejects([&]{timeline.active(op);});

  Fixture zero;const auto target=zero.add(6,{f(2)},{},"zero");zero.event("zero",99,99);
  wz3::EventTimeline instant(zero.p,zero.g);instant.begin_frame(5*65536);check(instant.active(target).size()==1,"zero-length events are initially always active");
  const auto& started=instant.start(0,2);check(started.fields[1]==5*65536 && started.fields[2]==7*65536,"default duration starts at first dispatch beat");
  instant.begin_frame(6*65536);instant.start(0,4);check(instant.event(0).fields[2]==7*65536,"subsequent execution must not restart an event");
  instant.begin_frame(7*65536);check(instant.active(target).empty(),"started event must expire");
  check(zero.p.events[0].fields[1]==99 && zero.p.events[0].fields[2]==99,"runtime scheduling changed the project");
  instant.reset();instant.begin_frame(0);const int rounding=std::fegetround();std::fesetround(FE_DOWNWARD);
  instant.start(0,1.5f/65536);check(instant.event(0).fields[2]==2 && std::fegetround()==FE_DOWNWARD,"event duration uses nearest-even and restores rounding");std::fesetround(rounding);
  instant.reset();instant.begin_frame(0);instant.start(0,0);instant.begin_frame(10);instant.start(0,0);check(instant.event(0).fields[1]==10 && instant.event(0).fields[2]==10,"zero default duration stays instantaneous");
  instant.reset();instant.begin_frame(2147483647);rejects([&]{instant.start(0,1);});check(instant.event(0).fields[1]==99,"overflow changed the event before validation");
}
void nested() {
  Fixture fx;const auto cube=fx.cube();const auto instance=fx.transform(cube);fx.animate(instance,0,6,2);
  const auto event=fx.add(6,{0},{instance},"inner");const auto timed=fx.add(5,{}, {instance},"first");
  const auto sibling=fx.transform(cube);fx.animate(sibling,0,6,2);
  // Event with a mesh result is a cached mesh input: ExecSceneInput does not
  // execute its Event handler, even when its event list is empty.
  const auto cached=fx.add(6,{0},{cube},"cached");
  const auto root=fx.viewport(fx.add(0xc1,{}, {event,timed,sibling,cached}));fx.animate(root,0,6);
  const auto outer=fx.add(6,{0},{root},"outer");const auto alias=fx.add(2,{}, {outer},"stored");
  const auto composite=fx.add(6,{0},{fx.add(0xe2,{0,0xffffffff,f(1)},{root})},"copy");
  fx.event("inner",0,4*65536,1,10);fx.event("inner",0,8*65536,2,20);fx.event("first",0,4*65536,1,30);fx.event("first",0,8*65536,2,40);
  fx.event("outer",0,8*65536,0,100);fx.p.events.back().interval={f(.25f),f(.75f)};
  fx.event("outer",3*65536,4*65536,0,200);fx.event("copy",0,8*65536);
  wz3::EventTimeline timeline(fx.p,fx.g);wz3::ScenePlayer player(fx.p,fx.g);timeline.begin_frame(2*65536);
  const auto frames=player.event_viewports(alias,timeline,1);check(frames.size()==1,"one active outer event");const auto& frame=frames[0];
  check(frame.instances.size()==5,"overlapping Event, first-only Time, sibling and cached mesh");
  near(frame.instances[0].world[12],120,"nested event matrix composed with enclosing event");near(frame.instances[1].world[12],110,"reverse event list order");
  near(frame.instances[0].world[13],.25f,"inner event independent normalized time");near(frame.instances[1].world[13],.5f,"second inner event time");
  near(frame.instances[2].world[12],140,"Time chooses FirstEvent");
  near(frame.instances[3].world[13],.375f,"sibling restores enclosing interval time");near(frame.camera.world[12],.375f,"camera uses enclosing event interval");
  near(frame.instances[4].world[12],100,"cached mesh event is not dispatched");
  timeline.begin_frame(3*65536+32768);const auto overlapping=player.event_viewports(outer,timeline,1);
  check(overlapping.size()==2,"overlapping outer events produce separate viewport executions");
  near(overlapping[0].instances[0].world[12],220,"outer events keep reverse file order");near(overlapping[1].instances[0].world[12],120,"outer transform restored between executions");
  rejects([&]{player.event_viewports(composite,timeline,1);});
  timeline.begin_frame(8*65536);check(player.event_viewports(outer,timeline,1).empty(),"outer end boundary suppresses its viewport");
  timeline.begin_frame(65536);check(player.event_viewports(outer,timeline,1).size()==1,"backward sample recomputes static event activation");
  rejects([&]{player.viewport(root,wz3::frame_environment(0),1);});
  check(player.event_viewports(outer,timeline,1).size()==1,"missing timeline failure poisoned subsequent traversal");
}
void theta(const char* file) {
  const auto p=wz3::read_project(file);const auto g=wz3::connect(p);wz3::EventTimeline timeline(p,g);wz3::ScenePlayer player(p,g);
  timeline.begin_frame(56*65536);const auto frames=player.event_viewports(g.names.at("scene.generation2"),timeline,4.0f/3);
  check(frames.size()==1 && frames[0].viewport_id==590 && frames[0].instances.size()==7,"saved generation2 event dispatch");
  near(frames[0].camera.world[12],-5.252972126f,"event-selected saved camera midpoint");
  timeline.begin_frame(64*65536);check(player.event_viewports(g.names.at("generation2"),timeline,1).empty(),"Theta event end boundary");
  for(const auto* name:{"cp1","cp2a"}) {
    for(const auto& event:p.events)if(event.name==name && !event.fields[10]) {
      const auto beat=std::int32_t((std::int64_t(std::bit_cast<std::int32_t>(event.fields[1]))+std::bit_cast<std::int32_t>(event.fields[2]))/2);
      timeline.begin_frame(beat);
      const auto result=player.event_viewports(g.names.at(name),timeline,4.0f/3);
      if(std::string_view(name)=="cp2a")check(beat==120*65536 && result.size()==1 && result[0].viewport_id==1622 && result[0].instances.size()==185,"Theta cp2a particle event at beat 120");
      else check(beat==72*65536 && result.size()==1 && result[0].viewport_id==1527 && result[0].instances.size()==9,"Theta cp1 nested events at beat 72");
      std::cout<<name<<" at beat "<<beat/65536.0f<<": "<<result[0].instances.size()<<" instances\n";
    }
  }
}
}
int main(int argc,char** argv) {
  try {check(argc==2,"expected Theta project");scheduling();nested();theta(argv[1]);std::cout<<"timeline scheduling and nested scene event dispatch passed\n";}
  catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
