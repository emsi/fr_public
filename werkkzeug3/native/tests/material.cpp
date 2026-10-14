// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/scene.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::uint32_t f(float value) {return std::bit_cast<std::uint32_t>(value);}
template<class F> void rejects(F fn) {bool caught=false;try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid material accepted");}
std::vector<std::uint32_t> parameters(unsigned flags=1) {
  std::vector<std::uint32_t> p(64);p[0]=0x300;p[9]=16;p[32]=f(8);p[48]=flags;p[51]=f(32);p[52]=0xff908070;p[53]=0xff203040;
  for(unsigned i=0;i<4;++i) {p[4+i]=2;p[24+i]=f(1);p[28+i]=0xff705030;p[60+i]=f(1);}
  p[34]=p[35]=p[36]=p[43]=p[44]=f(1);return p;
}
struct Fixture {
  wz3::Project p;wz3::Graph g;
  std::size_t add(unsigned cmd,std::vector<std::uint32_t> values={},std::vector<std::size_t> inputs={},std::vector<std::optional<std::size_t>> links={}) {
    wz3::Operator op;op.command=cmd;op.parameters=std::move(values);p.operators.push_back(op);g.nodes.push_back({true,true,std::move(inputs),std::move(links)});return p.operators.size()-1;
  }
  void animate(std::size_t id,unsigned variable,unsigned offset,unsigned store=0x91) {
    wz3::Animation load,write;load.fields={0,0,3,2,variable,0,0,0};write.fields={0,1,3,store,offset,5,0,0};p.operators[id].animations={load,write};
  }
};
void passes() {
  Fixture fx;std::vector<std::optional<std::size_t>> textures;
  for(unsigned i=0;i<7;++i)textures.push_back(fx.add(0x21));
  auto p=parameters(0x1f|0x200|0x400|0x100|0x4000);p[0]|=2;p[49]=7;p[56]=0x1003;p[58]=0x2002;p[60]=f(3);p[62]=f(4);
  const auto id=fx.add(0xd0,p,{},textures);wz3::MaterialRuntime runtime(fx.p,fx.g);const auto material=runtime.snapshot(id);
  check(material.material11_insert && material.passes.size()==5,"multipass initialization");const auto& ps=material.passes;
  check(ps[0].usage==wz3::MaterialUsage::base && ps[0].shader.words[0]==0x20303 && ps[0].shader.words[28]==0xff203040,"base flags and frozen ambient");
  check(ps[0].shader.textures[0]==textures[0] && ps[0].shader.words[23]==4 && ps[0].shader.words[19]==0x20,"base alpha test and vertex addition");
  check(ps[1].usage==wz3::MaterialUsage::light && ps[1].shader.words[0]==0x305282 && ps[1].shader.words[2]==5,"light state");
  check(ps[1].shader.textures[1]==textures[4] && ps[1].shader.words[5]==0x1003 && ps[1].shader.words[25]==f(3),"bump texture slot and scale");
  check(ps[2].usage==wz3::MaterialUsage::shadow && ps[2].shader.words[0]==0x70220 && ps[2].program==wz3::MaterialProgram::shadow,"shadow volume pass");
  check(ps[3].usage==wz3::MaterialUsage::postlight && ps[3].shader.words[0]==0x4280 && ps[3].shader.words[23]==12,"multiply-two texture pass");
  check(ps[4].usage==wz3::MaterialUsage::environment && ps[4].shader.words[1]==0x10 && ps[4].shader.words[17]==0x14 && ps[4].shader.textures[2]==textures[6],"environment mapping and alpha mask");
  for(const auto& pass:ps)check(pass.render_pass==7,"saved render pass order");
  auto env=wz3::frame_environment(0);runtime.execute(id,env);const auto updated=runtime.snapshot(id);
  check(updated.passes[1].shader.words[24]==p[24] && updated.passes[3].shader.words[31]==0xff808080,"alpha-tested runtime updates");

  Fixture single;auto q=parameters(0x10001|0x800);q[60]=f(2);q[62]=f(.5f);q[1]=1;
  const auto texture=single.add(0x21),sprite=single.add(0xd0,q,{}, {texture});wz3::MaterialRuntime sprites(single.p,single.g);const auto result=sprites.snapshot(sprite);
  check(result.passes.size()==1 && !result.material11_insert && result.passes[0].program==wz3::MaterialProgram::sprites,"sprite finalizer overrides instancing");
  check(result.passes[0].size==2 && result.passes[0].aspect==.5f && (result.passes[0].shader.words[0]&0x20000),"sprite dimensions and normal suppression");
  check(!result.passes[0].shader.textures[0] && !result.passes[0].shader.words[1],"shader 1.1 detail bump normalization");
}
void shared_updates() {
  Fixture fx;auto a=parameters(0x10001);a[60]=f(2);a[28]=0xffff0000;
  auto b=parameters(0x10001);b[60]=f(4);b[28]=0xff0000ff;
  const auto child=fx.add(0xd0,a),parent=fx.add(0xd0,b,{child}),added=fx.add(0xd1,{}, {child,parent});
  wz3::MaterialRuntime runtime(fx.p,fx.g);const auto before=runtime.snapshot(added);auto env=wz3::frame_environment(0);runtime.execute(parent,env);
  const auto c=runtime.snapshot(child),p=runtime.snapshot(parent),joined=runtime.snapshot(added);
  check(c.passes[0].shader.words[28]==0xff0000ff && p.passes[0].shader.words[28]==0xff0000ff && joined.passes[0].shader.words[28]==0xff0000ff,"Material_Add must share mutable shader objects");
  check(c.passes[0].size==2 && p.passes[0].size==4 && joined.passes[0].size==2,"pass metadata is copied independently of shader state");
  check(before.passes[0].shader.words[28]==0xffff0000,"snapshot changed after future material execution");
  runtime.reset();check(runtime.snapshot(child).passes[0].shader.words[28]==0xffff0000,"reset did not restore saved parameters");

  Fixture scope;const auto material=scope.add(0xd0,parameters());scope.animate(material,15,24);
  const auto alias=scope.add(3,{}, {material});scope.animate(alias,0,15,0x8f);
  const auto bad=scope.add(3);scope.g.nodes[bad].inputs={bad};
  wz3::MaterialRuntime scoped(scope.p,scope.g);auto init=wz3::frame_environment(16384);init.variables[15].fill(9);scoped.precalculate(alias,init);
  check(scoped.snapshot(material).passes[0].shader.words[24]==f(.25f) && init.variables[15][0]==9,"precalculation inherits and restores ancestor animation");
  auto later=wz3::frame_environment(49152);later.variables[15].fill(8);scoped.execute(alias,later);
  check(scoped.snapshot(material).passes[0].shader.words[24]==f(.75f) && later.variables[15][0]==8,"runtime material scoped variables");
  rejects([&]{scoped.snapshot(bad);});check(scoped.snapshot(material).passes.size()==1,"cycle failure poisoned material state");
}
void animation() {
  Fixture fx;auto p=parameters(0x1b);const auto id=fx.add(0xd0,p);fx.animate(id,0,53,0xbf);
  wz3::MaterialRuntime runtime(fx.p,fx.g);const auto before=runtime.snapshot(id);auto env=wz3::frame_environment(32768);runtime.execute(id,env);const auto after=runtime.snapshot(id);
  check(before.passes[0].shader.words[28]==0 && after.passes[0].shader.words[28]==0,"base ambient should stay at initialization value");
  check(after.passes.back().shader.words[30]==0x7f7f7f7f,"environment ambient updates with packed animation");
  check(after.passes[1].shader.words[28]==0x00908070 && (after.passes[1].shader.words[1]&0x40),"disabled specular masks diffuse alpha at execution");
  check(fx.p.operators[id].parameters[53]==0xff203040,"material animation modified saved recipe");
}
void scene_update_order() {
  Fixture fx;const auto material=fx.add(0xd0,parameters());fx.animate(material,0,28,0xb4);
  const auto cube=fx.add(0x81,{1,1,1,1,f(1),f(1),f(1),0,0,0,0,0,0});
  const auto mesh=fx.add(0x96,{0,0},{cube},{material});
  const auto hack=fx.add(0xd2,{}, {},{material}),scene=fx.add(0xc1,{}, {mesh,hack});
  const auto root=fx.add(0xf0,{3,3,0xff000000,0,0,0,0,0,f(-5),f(4096),f(.125f),0,0,f(1),f(1),0xff000000,f(4096),f(16)},{scene});
  wz3::ScenePlayer player(fx.p,fx.g);const auto first=player.viewport(root,wz3::frame_environment(49152),1);
  check(first.instances.size()==1 && first.materials.at(material).passes[0].shader.words[28]==0xffbf5030,"MatHack after mesh emission must affect the paint snapshot");
  const auto next=player.viewport(root,wz3::frame_environment(65536),1);
  check(next.materials.at(material).passes[0].shader.words[28]==0xffff5030 && first.materials.at(material).passes[0].shader.words[28]==0xffbf5030,"previous frame material snapshot is immutable");
}
void theta(const char* file) {
  const auto p=wz3::read_project(file);const auto g=wz3::connect(p);wz3::MaterialRuntime runtime(p,g);unsigned count=0,passes=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0xd0) {
    ++count;passes+=runtime.snapshot(id).passes.size();for(int beat:{0,16384,32768,49152,65536}) {auto env=wz3::frame_environment(beat);runtime.execute(id,env);}
  }
  check(count==65 && passes==120,"reachable Theta material/pass count");std::cout<<count<<" Theta materials, "<<passes<<" render passes\n";
  wz3::ScenePlayer player(p,g);for(auto id:{979u,980u}) {
    const auto frame=player.viewport(id,wz3::frame_environment(32768),4.0f/3);
    check(frame.effects.size()==1 && frame.effects[0].operator_id==961,"MatHack scene particle paint job");
    check(frame.effects[0].shader.words[0]==0x2200,"particle draw lost its saved material");
  }
  wz3::EventTimeline timeline(p,g);timeline.begin_frame(384*65536);auto env=wz3::frame_environment(timeline.beat());const wz3::SplineLibrary splines(p);
  const wz3::EventAnimationScope event(p.events[0],timeline.beat(),splines,env);
  const auto frame=player.viewport(1156,env,4.0f/3,wz3::scene_srt(p.events[0].transform),&timeline);
  check(frame.instances.size()==14 && frame.lights.size()==11 && !frame.materials.empty(),"generation10 material update and nested events");
}
}
int main(int argc,char** argv) {
  try {check(argc==2,"expected Theta project");passes();shared_updates();animation();scene_update_order();theta(argv[1]);std::cout<<"material phases, sharing, animation and scene updates passed\n";}
  catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
