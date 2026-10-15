// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "material20_reference.hpp"
#include "wz3/scene_draw.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action) {
  bool rejected=false;try {action();}catch(const std::runtime_error&) {rejected=true;}
  check(rejected,"invalid Material 2.0 accepted");
}
void compare(const wz3::Material& actual,const wz3::Material& expected) {
  check(!actual.material11_insert && actual.material20_insert==expected.material20_insert,"wrong material insert");
  check(actual.passes.size()==expected.passes.size(),"archived pass count differs");
  for(std::size_t i=0;i<actual.passes.size();++i) {
    const auto& a=actual.passes[i];const auto& e=expected.passes[i];
    check(a.usage==e.usage && a.program==e.program && a.render_pass==0 && a.size==1 && a.aspect==1,"archived pass metadata differs");
    check(bool(a.shader.material20)==bool(e.shader.material20),"archived shader class differs");
    if(a.shader.material20) {
      const auto& x=*a.shader.material20;const auto& y=*e.shader.material20;
      check(x.stage==y.stage && x.parameters==y.parameters && x.compiled_parameters==y.compiled_parameters,"archived shader parameters differ");
      check(x.textures==y.textures,"archived bitmap selection differs");
    }else check(a.shader.words[0]==e.shader.words[0] && a.shader.words[9]==e.shader.words[9],"archived shadow setup differs");
  }
}
struct Fixture {
  wz3::Project p;wz3::Graph g;
  std::size_t add(unsigned cmd,std::vector<std::uint32_t> parameters={},std::vector<std::size_t> inputs={},std::vector<std::optional<std::size_t>> links={}) {
    wz3::Operator op;op.command=cmd;op.parameters=std::move(parameters);p.operators.push_back(op);
    g.nodes.push_back({true,true,std::move(inputs),std::move(links)});return p.operators.size()-1;
  }
};
void synthetic() {
  for(unsigned variant=0;variant<8;++variant) {
    Fixture f;std::vector<std::optional<std::size_t>> links;
    std::array<int,7> ref_links;std::array<int,3> ref_inputs;
    for(unsigned i=0;i<7;++i) {ref_links[i]=int(f.add(0x21));links.push_back(ref_links[i]);}
    if(!(variant&1)){links[6].reset();ref_links[6]=-1;}
    const auto a=f.add(0x21),b=f.add(0x21),wrong=f.add(0xd0);ref_inputs={int(a),int(b),-2};
    std::vector<std::uint32_t> p(42);p[0]=(variant&2) ? 0x10 : 0;p[1]=0xff123456;p[3]=std::bit_cast<std::uint32_t>(16.f);
    // Input overrides may also create the optional reflection pass. An absent
    // or non-bitmap input retains the saved link, as in the original handler.
    p[8]=1u<<24;p[9]=2u<<24;p[10]=3u<<24;
    if(variant&4)p[18]=2u<<24;
    const auto id=f.add(0xd3,p,{a,b,wrong},links),joined=f.add(0xd1,{}, {id});
    wz3::MaterialRuntime runtime(f.p,f.g);const auto actual=runtime.snapshot(id);
    compare(actual,reference::material20(p,ref_links,ref_inputs));
    auto mesh=std::make_shared<wz3::Mesh>();mesh->vertices={{{-1,-1,2}},{{1,-1,2}},{{0,1,2}}};
    wz3::MeshFace face;face.count=3;face.vertices={0,1,2,0};face.material=id;face.pass=7;mesh->faces={face};
    wz3::SceneFrame scene;scene.instances={{0,mesh}};scene.materials[id]=actual;
    const auto queue=wz3::scene_draw_jobs(scene);
    check(queue.material20_inserts[7] && !queue.inserts[7],"draw list lost Material 2.0 insert identity");
    check(queue.jobs.size()>=3,"depth/albedo/ambient jobs missing");
    for(unsigned phase=0;phase<3;++phase)check(((queue.jobs[phase].sort_key>>16)&15)==phase,"depth/albedo/ambient order differs");
    const auto added=runtime.snapshot(joined);check(!added.material20_insert,"Add must not copy insert hooks");
    check(added.passes.size()==actual.passes.size(),"Add lost Material 2.0 passes");
    rejects([&]{wz3::material_fragment_shader(actual.passes[0].shader);});
  }
  Fixture f;std::vector<std::uint32_t> p(42);p[0]=0x10;p[1]=0xff123456;
  const auto id=f.add(0xd3,p),joined=f.add(0xd1,{}, {id});
  wz3::Animation load,write;load.fields={0,0,3,2,0,0,0,0};write.fields={0,1,3,0x91,3,5,0,0};f.p.operators[id].animations={load,write};
  wz3::MaterialRuntime runtime(f.p,f.g);const auto before=runtime.snapshot(id);runtime.snapshot(joined);
  auto env=wz3::frame_environment(32768);runtime.execute(id,env);auto updated=p;updated[3]=std::bit_cast<std::uint32_t>(.5f);
  compare(runtime.snapshot(id),reference::material20(p,{-1,-1,-1,-1,-1,-1,-1},{-1,-1,-1},updated));
  const auto added=runtime.snapshot(joined);
  check(added.passes[0].shader.material20->parameters[3]==updated[3],"Add did not share mutable parameters");
  check(before.passes[0].shader.material20->parameters[3]==0,"snapshot changed after update");
  check(added.passes[0].shader.material20->compiled_parameters[3]==0,"animation rebuilt compiled parameters");
  f.p.operators[id].parameters.pop_back();wz3::MaterialRuntime truncated(f.p,f.g);rejects([&]{truncated.snapshot(id);});
}
int bitmap(const wz3::Project& p,const wz3::Graph& g,std::size_t id) {
  const auto handle=id;
  for(unsigned depth=0;depth<512;++depth) {
    const auto cmd=p.operators.at(id).command;const auto& n=g.nodes.at(id);
    if(cmd==1) {check(n.links.size()==1 && n.links[0],"invalid fixture Load");id=*n.links[0];}
    else if(cmd==2 || cmd==3) {check(n.inputs.size()==1,"invalid fixture alias");id=n.inputs[0];}
    else return wz3::operator_info(cmd).category=="BITMAP" ? int(handle) : -2;
  }
  throw std::runtime_error("fixture alias cycle");
}
void debris(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::MaterialRuntime runtime(p,g);wz3::SplineLibrary splines(p);
  unsigned count=0,passes=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0xd3) {
    const auto& op=p.operators[id];const auto& n=g.nodes[id];std::array<int,7> links;std::array<int,3> inputs;links.fill(-1);inputs.fill(-1);
    for(std::size_t i=0;i<n.links.size();++i)if(n.links[i])links.at(i)=bitmap(p,g,*n.links[i]);
    for(std::size_t i=0;i<n.inputs.size();++i)inputs.at(i)=bitmap(p,g,n.inputs[i]);
    const auto program=wz3::compile_animation(op,splines);auto zero=wz3::frame_environment(0);
    const wz3::AnimationScope initial(program,splines,op.parameters,zero);const auto actual=runtime.snapshot(id);
    check(initial.parameters.size()==38,"expected Debris's pre-color-correction layout");
    auto original=initial.parameters;original.insert(original.end(),{0,std::bit_cast<std::uint32_t>(1.f),std::bit_cast<std::uint32_t>(1.f),0});
    compare(actual,reference::material20(original,links,inputs));++count;passes+=actual.passes.size();
    for(auto beat:{16384,32768,65536}) {
      auto env=wz3::frame_environment(beat);runtime.execute(id,env);auto expected_env=wz3::frame_environment(beat);
      const wz3::AnimationScope update(program,splines,op.parameters,expected_env,initial.parameters);
      auto values=update.parameters;values.insert(values.end(),{0,std::bit_cast<std::uint32_t>(1.f),std::bit_cast<std::uint32_t>(1.f),0});
      compare(runtime.snapshot(id),reference::material20(original,links,inputs,values));
    }
  }
  check(count==124,"reachable Debris Material 2.0 count changed");
  std::cout<<count<<" Debris materials, "<<passes<<" passes: bitmap selection and updates match archived Init/Exec handlers\n";
}
}
int main(int argc,char** argv) {
  try {check(argc==2,"expected Debris project");synthetic();debris(argv[1]);}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
