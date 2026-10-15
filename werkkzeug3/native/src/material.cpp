// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Init/Exec_Material_Material, Material_Add and sMaterial11::LoadDefaults/Compile.
#include "wz3/material.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <stdexcept>

namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
float real(std::span<const std::uint32_t> p,unsigned index) {
  require(index<p.size(),"material parameter overrun");const auto value=as_float(p[index]);
  require(std::isfinite(value) && std::abs(value)<=1e15f,"invalid material numeric parameter");return value;
}
struct Visit {
  std::vector<bool>& visiting;std::size_t id;
  Visit(std::vector<bool>& v,std::size_t i):visiting(v),id(i) {require(!v[i],"cycle in material graph");v[i]=true;}
  ~Visit() {visiting[id]=false;}
};
struct SharedPass {
  std::shared_ptr<MaterialShader> shader;
  MaterialUsage usage=MaterialUsage::other;MaterialProgram program=MaterialProgram::static_mesh;
  std::int32_t render_pass{};float size=1,aspect=1;
};
struct SharedMaterial {std::vector<SharedPass> passes;bool insert{},insert20{};};
MaterialShader defaults(std::size_t id) {
  MaterialShader shader;shader.source_operator=id;shader.words[0]=0x300;shader.words[32]=std::bit_cast<std::uint32_t>(8.0f);
  for(unsigned i=0;i<4;++i) {shader.words[4+i]=2;shader.words[28+i]=0xffffffff;}
  return shader;
}
void validate(std::span<const std::uint32_t> p) {
  require(p.size()==64,"unsupported Material parameter layout");
  for(unsigned i=24;i<28;++i)real(p,i);
  real(p,32);for(unsigned i=34;i<48;++i)real(p,i);
  real(p,51);for(unsigned i=60;i<64;++i)real(p,i);
}
SharedMaterial compile(std::size_t id,std::span<const std::uint32_t> p,const std::array<std::optional<std::size_t>,8>& textures) {
  validate(p);MaterialShader base;std::copy_n(p.begin(),48,base.words.begin());base.source_operator=id;
  SharedMaterial out;const auto flags=p[48];
  const auto program=(flags&0x800) ? MaterialProgram::instances : MaterialProgram::static_mesh;
  const auto add=[&](MaterialShader shader,MaterialUsage usage,MaterialProgram finalizer) -> SharedPass& {
    // Shader model 1.1 disables its unsupported detail-bump slot at compile time.
    if(shader.words[1]&1) {shader.textures[0].reset();shader.words[1]&=~1u;}
    out.passes.push_back({std::make_shared<MaterialShader>(std::move(shader)),usage,finalizer,std::bit_cast<std::int32_t>(p[49]),1,1});
    return out.passes.back();
  };
  if(!(flags&0x1e)) {
    std::copy_n(textures.begin(),4,base.textures.begin());
    const auto phase=(flags>>20)&15;require(phase<=8,"invalid material phase insertion");
    auto finalizer=program;
    if(flags&0x30000) {
      base.words[0]|=0x20000;
      switch((flags>>16)&3) {case 1:finalizer=MaterialProgram::sprites;break;case 2:finalizer=MaterialProgram::thick_lines;break;default:break;}
    }
    auto& pass=add(base,phase ? MaterialUsage(phase-1) : MaterialUsage::other,finalizer);
    if(flags&0x30000) {pass.size=real(p,60);pass.aspect=real(p,62);}
  } else {
    out.insert=true;
    auto shader=defaults(id);auto& w=shader.words;
    w[0]=0x20300|(p[0]&2);w[28]=(flags&2) ? p[53] : 0x00ffffff;
    w[9]=0x10;w[19]=0x20;w[23]=13;
    if(flags&0x400) {shader.textures[0]=textures[0];w[4]=p[4];w[24]=p[24];w[23]=4;w[0]|=1;}
    add(shader,MaterialUsage::base,program);
    if(flags&2) {
      shader=defaults(id);w[0]=0x300280|(p[0]&2)|((flags&0x4000) ? 0x5000 : 0x2000);
      w[2]=5;w[32]=p[51];w[28]=p[52];w[8]=0x10;w[9]=0x40;w[23]=13;
      std::copy_n(p.begin()+34,14,w.begin()+34);
      if(textures[4]) {shader.textures[1]=textures[4];w[5]=p[56];w[25]=p[60];}
      if(!(flags&0x200))w[1]|=0x40;else if(flags&0x2000)w[1]|=0x80;
      add(shader,MaterialUsage::light,program);
    }
    if(flags&4) {
      shader=defaults(id);w[0]=0x70220;w[9]=0x10;
      add(shader,MaterialUsage::shadow,MaterialProgram::shadow);
    }
    if(flags&8) {
      shader=base;w[0]=0x4280;std::copy_n(textures.begin(),4,shader.textures.begin());w[23]=12;
      add(shader,MaterialUsage::postlight,program);
    }
    if(flags&16) {
      shader=defaults(id);w[0]=0x280|((flags&0x8000) ? 0 : 0x2000);w[1]=(flags&0x100) ? 0x10 : 0x20;
      shader.textures[2]=textures[6];w[30]=p[53];w[6]=p[58];w[26]=p[62];w[17]=0x10;
      std::copy_n(p.begin()+34,14,w.begin()+34);
      if(flags&0x4000) {shader.textures[0]=textures[0];w[4]=p[4];w[24]=p[24];w[17]|=4;}
      add(shader,MaterialUsage::environment,program);
    }
  }
  return out;
}
void update(SharedMaterial& material,std::span<const std::uint32_t> p) {
  validate(p);const auto flags=p[48];
  for(auto& pass:material.passes) {
    require(!pass.shader->material20,"Material 1.1 cannot update Material 2.0 shader parameters");
    auto& w=pass.shader->words;
    switch(pass.usage) {
    case MaterialUsage::light:
      w[32]=p[51];w[28]=p[52];if(!(flags&0x200))w[28]&=0x00ffffff;
      w[24]=(flags&0x1400) ? p[24] : p[61];w[25]=p[60];std::copy_n(p.begin()+34,14,w.begin()+34);break;
    case MaterialUsage::postlight:
      std::copy_n(p.begin()+24,24,w.begin()+24);if(flags&0x400)w[31]=0xff808080;break;
    case MaterialUsage::environment:w[30]=p[53];w[26]=p[62];break;
    case MaterialUsage::other:
      std::copy_n(p.begin()+24,24,w.begin()+24);
      if(flags&0x30000) {pass.size=real(p,60);pass.aspect=real(p,62);}break;
    default:break; // The original keeps base/shadow parameters from initialization.
    }
  }
}
void validate20(std::span<const std::uint32_t> p) {
  require(p.size()==38 || p.size()==42,"unsupported Material 2.0 parameter layout");
  real(p,3);real(p,4);for(unsigned i=12;i<16;++i)real(p,i);
  for(unsigned i=20;i<p.size();++i)if(i!=23)real(p,i);
}
std::array<std::uint32_t,42> parameters20(std::span<const std::uint32_t> p) {
  validate20(p);std::array<std::uint32_t,42> out{};
  // WerkDoc::Read initializes the current editor defaults, then copies the
  // saved words. Debris predates the four color-correction parameters.
  out[39]=out[40]=std::bit_cast<std::uint32_t>(1.f);
  std::copy(p.begin(),p.end(),out.begin());return out;
}
SharedMaterial compile20(std::size_t id,std::span<const std::uint32_t> p,const std::array<std::optional<std::size_t>,7>& textures) {
  validate20(p);SharedMaterial out;out.insert20=true;
  const auto add=[&](Material20Stage stage,MaterialUsage usage) {
    MaterialShader shader;shader.source_operator=id;auto& m=shader.material20.emplace();
    m.stage=stage;m.parameters=parameters20(p);
    m.compiled_parameters=m.parameters;m.textures=textures;
    out.passes.push_back({std::make_shared<MaterialShader>(std::move(shader)),usage,MaterialProgram::static_mesh,0,1,1});
  };
  // Init_Material_Material20, REALMAT=1 in the archived genmaterial.cpp.
  add(Material20Stage::depth,MaterialUsage::base);
  add(Material20Stage::texture,MaterialUsage::prelight);
  add(Material20Stage::ambient,MaterialUsage::ambient);
  if(p[0]&0x10) {
    auto shadow=defaults(id);shadow.words[0]=0x70220;shadow.words[9]=0x10;
    out.passes.push_back({std::make_shared<MaterialShader>(std::move(shadow)),MaterialUsage::shadow,MaterialProgram::shadow,0,1,1});
  }
  add(Material20Stage::light,MaterialUsage::light);
  if(textures[6])add(Material20Stage::environment,MaterialUsage::postlight);
  return out;
}
void update20(SharedMaterial& material,std::span<const std::uint32_t> p) {
  validate20(p);
  for(auto& pass:material.passes)if(pass.usage!=MaterialUsage::shadow) {
    require(pass.shader->material20.has_value(),"Material 2.0 pass has a different shader layout");
    pass.shader->material20->parameters=parameters20(p);
  }
}
}
struct MaterialRuntime::Impl {
  const Project& project;const Graph& graph;SplineLibrary splines;
  std::vector<std::shared_ptr<SharedMaterial>> cache;
  std::vector<std::optional<AnimationProgram>> programs;
  std::vector<std::vector<std::uint32_t>> previous;
  std::vector<bool> visiting;
  Impl(const Project& p,const Graph& g):project(p),graph(g),splines(p),cache(p.operators.size()),programs(cache.size()),previous(cache.size()),visiting(cache.size()) {
    require(g.nodes.size()==cache.size(),"material graph size mismatch");
  }
  void valid(std::size_t id) const {require(id<cache.size() && graph.nodes[id].active,"invalid or inactive material operator");}
  std::size_t alias(std::size_t id) const {
    const auto& node=graph.nodes[id];
    if(project.operators[id].command==1) {require(node.links.size()==1 && node.links[0],"unresolved material Load");return *node.links[0];}
    require(node.inputs.size()==1,"material alias needs one input");return node.inputs[0];
  }
  std::string_view category(std::size_t id) const {
    for(unsigned depth=0;depth<512;++depth) {valid(id);const auto cmd=project.operators[id].command;if(cmd<1 || cmd>3)return operator_info(cmd).category;id=alias(id);}
    throw std::runtime_error("cycle or excessive material alias depth");
  }
  const AnimationProgram& program(std::size_t id) {
    if(!programs[id])programs[id]=compile_animation(project.operators[id],splines);
    return *programs[id];
  }
  std::shared_ptr<SharedMaterial> prepare(std::size_t id,AnimationEnvironment& env,unsigned depth=0) {
    valid(id);if(cache[id])return cache[id];require(depth<512,"material initialization depth limit");Visit visit(visiting,id);
    const auto& op=project.operators[id];const auto& node=graph.nodes[id];
    const AnimationScope scope(program(id),splines,op.parameters,env);
    std::shared_ptr<SharedMaterial> result;
    if(op.command>=1 && op.command<=3)result=prepare(alias(id),env,depth+1);
    else if(op.command==0xd0 || op.command==0xd1) {
      require(op.command==0xd1 ? !node.inputs.empty() && scope.parameters.empty() : node.inputs.size()<=1,"invalid material input layout");
      result=std::make_shared<SharedMaterial>();
      for(auto input:node.inputs) {
        const auto child=prepare(input,env,depth+1);require(result->passes.size()+child->passes.size()<=256,"material pass limit");
        result->passes.insert(result->passes.end(),child->passes.begin(),child->passes.end());
      }
      if(op.command==0xd0) {
        require(node.links.size()<=8,"material texture slot limit");std::array<std::optional<std::size_t>,8> textures{};
        for(std::size_t i=0;i<node.links.size();++i)if(node.links[i]) {
          require(category(*node.links[i])=="BITMAP","material link is not a bitmap");textures[i]=node.links[i];
        }
        auto own=compile(id,scope.parameters,textures);require(result->passes.size()+own.passes.size()<=256,"material pass limit");
        result->passes.insert(result->passes.end(),own.passes.begin(),own.passes.end());
        // Material_Add copies the pass records but does not inherit Insert.
        result->insert=node.inputs.empty() && own.insert;
      }
    } else if(op.command==0xd3) {
      validate20(scope.parameters);require(node.inputs.size()<=3 && node.links.size()<=7,"invalid Material 2.0 texture layout");
      std::array<std::optional<std::size_t>,7> textures{};const unsigned paramap[7]={8,9,10,11,16,17,18};
      for(unsigned slot=0;slot<7;++slot) {
        if(slot<node.links.size() && node.links[slot] && category(*node.links[slot])=="BITMAP")textures[slot]=node.links[slot];
        const auto input=(scope.parameters[paramap[slot]]>>24)&3;
        // Original initialization keeps the linked bitmap if an override is
        // absent or has another class. It does not inherit material passes.
        if(input && input<=node.inputs.size() && category(node.inputs[input-1])=="BITMAP")textures[slot]=node.inputs[input-1];
      }
      result=std::make_shared<SharedMaterial>(compile20(id,scope.parameters,textures));
    } else throw std::runtime_error("material operator "+std::to_string(id)+" ("+std::string(operator_info(op.command).name)+") is not ported");
    previous[id]=scope.parameters;cache[id]=result;return result;
  }
  void execute(std::size_t id,AnimationEnvironment& env,std::size_t& visits,unsigned depth=0) {
    valid(id);require(depth<512 && ++visits<=1000000,"material execution limit");
    // KOp::Exec returns immediately for cached bitmaps, even through aliases.
    if(category(id)=="BITMAP")return;
    require(cache[id]!=nullptr,"material was not precalculated");Visit visit(visiting,id);
    const auto& op=project.operators[id];const auto& node=graph.nodes[id];
    const AnimationScope scope(program(id),splines,op.parameters,env,previous[id]);
    require(!scope.changed,"animated material regeneration is not ported");
    if(op.command==1)execute(alias(id),env,visits,depth+1);
    else {
      for(auto child:node.inputs)execute(child,env,visits,depth+1);
      if(!(op.convention&0x10000000))for(auto link:node.links)if(link)execute(*link,env,visits,depth+1);
      if(op.command==0xd0)update(*cache[id],scope.parameters);
      else if(op.command==0xd3)update20(*cache[id],scope.parameters);
      else require(op.command==2 || op.command==3 || op.command==0xd1,"unsupported material execution");
    }
    previous[id]=scope.parameters;
  }
};
MaterialRuntime::MaterialRuntime(const Project& p,const Graph& g):impl(std::make_unique<Impl>(p,g)) {}
MaterialRuntime::~MaterialRuntime()=default;
void MaterialRuntime::precalculate(std::size_t id,AnimationEnvironment& env) {impl->prepare(id,env);}
void MaterialRuntime::execute(std::size_t id,AnimationEnvironment& env) {
  auto initial=frame_environment(0);impl->prepare(id,initial);std::size_t visits=0;impl->execute(id,env,visits);
}
Material MaterialRuntime::snapshot(std::size_t id) {
  auto initial=frame_environment(0);const auto source=impl->prepare(id,initial);Material out;out.material11_insert=source->insert;out.material20_insert=source->insert20;
  for(const auto& pass:source->passes)out.passes.push_back({*pass.shader,pass.usage,pass.program,pass.render_pass,pass.size,pass.aspect});
  return out;
}
void MaterialRuntime::reset() {impl=std::make_unique<Impl>(impl->project,impl->graph);}
void write_material(std::ostream& out,const Material& material) {
  out<<std::setprecision(9)<<"{\"material11_insert\":"<<(material.material11_insert ? "true" : "false")
    <<",\"material20_insert\":"<<(material.material20_insert ? "true" : "false")<<",\"passes\":[";
  for(std::size_t i=0;i<material.passes.size();++i) {
    const auto& p=material.passes[i];out<<(i ? "," : "")<<"{\"source_operator\":"<<p.shader.source_operator<<",\"usage\":"<<int(p.usage)
      <<",\"program\":"<<int(p.program)<<",\"render_pass\":"<<p.render_pass<<",\"size\":"<<p.size<<",\"aspect\":"<<p.aspect<<",\"words\":[";
    for(unsigned j=0;j<48;++j)out<<(j ? "," : "")<<p.shader.words[j];
    out<<"],\"textures\":[";
    for(unsigned j=0;j<4;++j)out<<(j ? "," : "")<<(p.shader.textures[j] ? std::to_string(*p.shader.textures[j]) : "null");
    out<<']';
    if(p.shader.material20) {
      const auto& m=*p.shader.material20;out<<",\"material20\":{\"stage\":"<<int(m.stage)<<",\"parameters\":[";
      for(unsigned j=0;j<42;++j)out<<(j ? "," : "")<<m.parameters[j];
      out<<"],\"compiled_parameters\":[";
      for(unsigned j=0;j<42;++j)out<<(j ? "," : "")<<m.compiled_parameters[j];
      out<<"],\"textures\":[";
      for(unsigned j=0;j<7;++j)out<<(j ? "," : "")<<(m.textures[j] ? std::to_string(*m.textures[j]) : "null");
      out<<"]}";
    }
    out<<'}';
  }
  out<<"]}";require(bool(out),"cannot write material snapshot");
}
} // namespace wz3
