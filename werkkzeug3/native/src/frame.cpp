// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Ordered execution follows kdoc.cpp and genoverlay.cpp. The four-size target
// pool belongs to the .k editor/Theta path, not the later Debris player hack.
#include "wz3/frame.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <stdexcept>

namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
float real(std::span<const std::uint32_t> p,unsigned i) {
  require(i<p.size(),"IPP parameter overrun");const auto v=as_float(p[i]);
  require(std::isfinite(v) && std::abs(v)<=1e6f,"invalid IPP numeric parameter");return v;
}
struct Visit {
  std::vector<bool>& flags;std::size_t id;
  Visit(std::vector<bool>& f,std::size_t i):flags(f),id(i) {require(!flags[i],"cycle in frame graph");flags[i]=true;}
  ~Visit() {flags[id]=false;}
};
MaterialShader unlit() {MaterialShader s;s.words[0]=0x20002;return s;}
constexpr unsigned operations[]={0x20,0x80,0xa0,0x30,0x40,0x50,0x98,0xb0};
}
struct FramePlayer::Impl {
  const Project& project;const Graph& graph;SplineLibrary splines;ScenePlayer scenes;EventTimeline timeline;
  std::vector<std::optional<AnimationProgram>> programs;
  std::vector<std::vector<std::uint32_t>> previous;
  std::vector<bool> visiting,prepared;
  std::vector<std::size_t> outputs;
  struct Target {std::optional<std::size_t> owner;std::int64_t refs{};unsigned size{};};
  std::array<Target,13> pool;
  std::optional<unsigned> last;
  std::size_t master{};DemoFrame result;
  Impl(const Project& p,const Graph& g,const TextureOptions& options):project(p),graph(g),splines(p),scenes(p,g,options),timeline(p,g),
      programs(p.operators.size()),previous(programs.size()),visiting(programs.size()),prepared(programs.size()),outputs(programs.size()) {
    require(g.nodes.size()==programs.size(),"frame graph size mismatch");
    for(const auto& node:g.nodes)if(node.active) {
      for(auto id:node.inputs) {require(id<outputs.size(),"invalid input");++outputs[id];}
      for(auto id:node.links)if(id) {require(*id<outputs.size(),"invalid link");++outputs[*id];}
    }
    for(unsigned i=0;i<13;++i)pool[i].size=i/3;
  }
  void valid(std::size_t id) const {require(id<programs.size() && graph.nodes[id].active,"invalid or inactive frame operator");}
  std::size_t alias(std::size_t id) const {
    const auto& node=graph.nodes[id];
    if(project.operators[id].command==1) {require(node.links.size()==1 && node.links[0],"unresolved frame Load");return *node.links[0];}
    require(node.inputs.size()==1,"frame alias needs one input");return node.inputs[0];
  }
  std::size_t concrete(std::size_t id) const {
    for(unsigned depth=0;depth<512;++depth) {
      valid(id);const auto cmd=project.operators[id].command;
      if((cmd<1 || cmd>3) && cmd!=5 && cmd!=6 && cmd!=0x16)return id;
      id=alias(id);
    }
    throw std::runtime_error("cycle or excessive frame alias depth");
  }
  std::string_view category(std::size_t id) const {return operator_info(project.operators[concrete(id)].command).category;}
  const AnimationProgram& program(std::size_t id) {
    if(!programs[id])programs[id]=compile_animation(project.operators[id],splines);
    return *programs[id];
  }
  void prepare(std::size_t id,AnimationEnvironment& env,unsigned depth=0) {
    valid(id);if(prepared[id])return;require(depth<512,"frame preparation depth limit");
    // MaterialRuntime owns its own animation scope and shared initialization.
    if(category(id)=="MATERIAL") {scenes.materials().precalculate(id,env);prepared[id]=true;return;}
    Visit visit(visiting,id);const auto& op=project.operators[id];
    const AnimationScope scope(program(id),splines,op.parameters,env);
    for(auto child:graph.nodes[id].inputs)prepare(child,env,depth+1);
    for(auto child:graph.nodes[id].links)if(child)prepare(*child,env,depth+1);
    scenes.prepare_effect(id,scope.parameters);previous[id]=scope.parameters;prepared[id]=true;
  }
  void reset_pool() {for(unsigned i=0;i<12;++i)pool[i].refs=0;last.reset();}
  std::optional<unsigned> find(std::size_t id) {
    for(unsigned i=0;i<12;++i)if(pool[i].refs && pool[i].owner==id) {++result.cache_hits;return i;}
    return {};
  }
  unsigned alloc(std::optional<std::size_t> owner,unsigned size,std::int64_t refs) {
    require(size<=5 && refs>=0,"invalid IPP allocation");
    for(unsigned i=0;i<12;++i)if(!pool[i].refs && pool[i].size==size) {pool[i].refs=refs;pool[i].owner=owner;return i;}
    if(size<4)++result.screen_fallbacks;
    return 12;
  }
  void release(unsigned rt) {if(rt<12) {require(pool[rt].refs>0,"IPP released an unreferenced target");--pool[rt].refs;}}
  FrameTarget target(unsigned rt) const {
    if(rt==12) {const auto& s=result.surfaces.at(master);return {master,s.width,s.height};}
    const auto& s=result.surfaces.at(rt+1);auto width=s.width,height=s.height;
    if(pool[rt].size==3) {const auto& m=result.surfaces.at(master);width=std::min(width,m.width);height=std::min(height,m.height);}
    return {rt+1,width,height};
  }
  void append(std::size_t id,unsigned rt,std::variant<FrameClear,FrameQuad,SceneFrame> draw) {
    require(result.commands.size()<100000,"frame draw command limit");result.commands.push_back({id,target(rt),std::move(draw)});
  }
  void bind(FrameQuad& quad,unsigned slot,unsigned rt,FrameRect uv={0,0,1,1}) const {
    require(rt<12,"screen has no sampleable bitmap");const auto vp=target(rt);const auto& s=result.surfaces.at(vp.surface);
    const float sx=float(vp.width)/s.width,sy=float(vp.height)/s.height;
    quad.shader.textures[slot]=vp.surface;quad.shader.words[4+slot]=0x101;
    quad.textures[slot]=FrameTexture{true,vp.surface};quad.uv[slot]={uv[0]*sx,uv[1]*sy,uv[2]*sx,uv[3]*sy};
  }
  void copy(std::size_t id,unsigned in,unsigned out,std::uint32_t color=0xffffffff,float zoom=1) {
    if(in==12)return;
    FrameQuad q;q.shader=unlit();q.shader.words[9]=0x10;q.shader.words[11]=0x40;q.shader.words[23]=0x84;q.shader.words[28]=color;
    const float h=zoom*.5f;bind(q,0,in,{.5f-h,.5f-h,.5f+h,.5f+h});append(id,out,std::move(q));
  }
  std::optional<unsigned> blend4(std::size_t id,std::optional<std::size_t> owner,std::optional<unsigned> in,
      std::array<FrameRect,4> rect,bool offsets,float amplify,unsigned size,std::int64_t refs) {
    if(!in)return {};
    if(size==5)size=pool[*in].size;
    const auto out=alloc(owner,size,refs);
    if(*in<12) {
      const auto& s=result.surfaces.at(*in+1);
      if(offsets)for(auto& r:rect) {r[0]/=s.width;r[1]/=s.height;r[2]=r[2]/s.width+1;r[3]=r[3]/s.height+1;}
      const auto alpha=std::uint32_t(std::clamp(amplify*64.0f,0.0f,255.0f));
      for(unsigned pass=0;pass<2;++pass) {
        FrameQuad q;q.shader=unlit();if(pass)q.shader.words[0]|=0x2000;
        q.shader.words[11]=0x18;q.shader.words[12]=0x28;q.shader.words[23]=0xe0;q.shader.words[28]=alpha<<24;
        bind(q,0,*in,rect[pass*2]);bind(q,1,*in,rect[pass*2+1]);append(id,out,std::move(q));
      }
    }
    release(*in);return out;
  }
  std::size_t bitmap_target(std::size_t id) {
    id=concrete(id);const auto& op=project.operators[id];const auto& p=op.parameters;
    require(op.command==0x26 && p.size()==3 && p[0]<=12 && p[1]<=12 && p[2]==1,"expected RGBA8 bitmap render target");
    result.surfaces.emplace(13+id,FrameSurface{1u<<p[0],1u<<p[1]});return 13+id;
  }
  void execute(std::size_t id,AnimationEnvironment& env,const SceneMatrix& world,unsigned depth=0) {
    valid(id);require(depth<512 && ++result.visited_operators<=1000000,"frame traversal limit");
    const auto cat=category(id);if(cat=="BITMAP")return;
    if(cat=="MATERIAL") {scenes.materials().execute(id,env);return;}
    Visit visit(visiting,id);const auto& op=project.operators[id];const auto& node=graph.nodes[id];
    // ScenePlayer evaluates this scope and its children exactly once. The size
    // is evaluated separately below, with variable stores rolled back first.
    if(op.command==0xf0) {
      unsigned size;
      {const AnimationScope scope(program(id),splines,op.parameters,env,previous[id]);require(!scope.parameters.empty(),"missing viewport size");size=scope.parameters[0];}
      if(auto cached=find(id))last=*cached;
      else {auto scene=scenes.viewport(id,env,float(result.width)/result.height,world,&timeline);const auto rt=alloc(id,size,outputs[id]);append(id,rt,std::move(scene));last=rt;}
      return;
    }
    const AnimationScope scope(program(id),splines,op.parameters,env,previous[id]);const auto& p=scope.parameters;
    require(!scope.changed,"frame cache regeneration is not ported");previous[id]=p;
    const auto children=[&]() {
      for(auto child:node.inputs)execute(child,env,world,depth+1);
      if(!(op.convention&0x10000000))for(auto child:node.links)if(child)execute(*child,env,world,depth+1);
    };
    const auto count=outputs[id];
    switch(op.command) {
    case 1:execute(alias(id),env,world,depth+1);break;
    case 2:case 3:children();break;
    case 5:case 6:
      require(node.inputs.size()==1 && (op.command==5 ? p.empty() : p.size()==1),"invalid frame Event/Time layout");
      for(auto event_id:timeline.active(id)) {
        const auto& event=op.command==6 ? timeline.start(event_id,real(p,0)) : timeline.event(event_id);
        const EventAnimationScope event_scope(event,timeline.beat(),splines,env,event_id);
        execute(node.inputs[0],env,scene_combine(world,scene_srt(event.transform)),depth+1);
        if(op.command==5)break;
      }
      break;
    case 0x0d:
      require(p.empty(),"unsupported Demo parameters");
      for(auto child:node.inputs) {
        const auto type=category(child);
        if(type=="DEMO")execute(child,env,world,depth+1);
        else if(type=="IPP") {reset_pool();execute(child,env,world,depth+1);if(last)copy(id,*last,12);}
        else throw std::runtime_error("Demo input is neither DEMO nor IPP");
      }
      break;
    case 0xea:
      require(p.empty() && !node.inputs.empty(),"invalid IPP Select layout");children();
      // The archive retained a stale 'count' Exec argument after removing
      // OPC_OUTPUTCOUNT. Repair the intended fan-out accounting explicitly.
      if(last && *last<12) {pool[*last].refs+=std::int64_t(count)-1;require(pool[*last].refs>=0,"negative Select target references");}
      break;
    case 0xe2: {
      require(p.size()==3,"invalid IPP Copy layout");children();if(!last)break;const auto in=*last;
      if(auto cached=find(id))last=*cached;
      else {const auto rt=alloc(id,p[0]==5 ? pool[in].size : p[0],count);copy(id,in,rt,p[1],real(p,2));last=rt;}
      break;
    }
    case 0xe3: {
      require(p.size()==5 && p[4]<=32,"invalid IPP Blur layout");children();if(auto cached=find(id)) {last=*cached;break;}
      float radius=real(p,1);unsigned stages=0;do {radius*=.5f;++stages;}while(stages<p[4] && radius>.5f);
      constexpr int kernel[2][8]={{-1,-1,1,-1,-1,1,1,1},{0,-1,-1,0,1,0,0,1}};
      while(stages--) {
        std::array<FrameRect,4> rect;for(unsigned i=0;i<4;++i) {const float x=kernel[p[3]&1][i*2]*radius,y=kernel[p[3]&1][i*2+1]*radius*.5f;rect[i]={x,y,x,y};}
        last=blend4(id,stages ? std::nullopt : std::optional(id),last,rect,true,
          stages && !(p[3]&2) ? 1 : real(p,2),stages && p[0]==4 ? 0 : p[0],stages ? 1 : count);radius*=2;
      }
      break;
    }
    case 0xe4: {
      require(p.size()==6 && p[1]<=32,"invalid IPP Crashzoom layout");children();if(auto cached=find(id)) {last=*cached;break;}
      float zoom=real(p,2);if(zoom<1)zoom=2-zoom;
      for(unsigned steps=p[1];steps;) {
        float z=1;const float zf=std::pow(zoom,-std::pow(4.0f,-float(steps)));--steps;std::array<FrameRect,4> rect;
        for(auto& r:rect) {const float x=real(p,4)*(1-z),y=real(p,5)*(1-z);r={x,y,x+z,y+z};z*=zf;}
        last=blend4(id,id,last,rect,false,steps ? 1 : real(p,3),steps && p[0]==4 ? 0 : p[0],steps ? 1 : count);
      }
      break;
    }
    case 0xe6:case 0xe7: {
      require(p.size()==4,"invalid IPP Color/Merge layout");if(auto cached=find(id)) {last=*cached;break;}
      FrameQuad q;q.shader=unlit();q.shader.words[15]=0x60;q.shader.words[30]=p[3];
      std::array<unsigned,2> in{};const bool merge=op.command==0xe7;
      if(merge) {
        require(node.inputs.size()==2 && p[1]<8 && p[2]<=255,"unsupported IPP Merge operation");
        for(unsigned i=0;i<2;++i) {last.reset();execute(node.inputs[i],env,world,depth+1);const auto got=last;last.reset();if(!got || *got==12)return;in[i]=*got;bind(q,i,in[i]);}
        q.shader.words[11]=0x10;q.shader.words[12]=operations[p[1]];q.shader.words[28]=p[2]<<24;
      } else {
        require(p[2]<8,"unsupported IPP Color operation");children();if(!last || *last==12)break;in[0]=*last;bind(q,0,in[0]);
        q.shader.words[9]=0x10;q.shader.words[11]=operations[p[2]];q.shader.words[28]=p[1];
      }
      const auto out=alloc(id,p[0]==5 ? pool[in[0]].size : p[0],count);append(id,out,std::move(q));
      release(in[0]);if(merge)release(in[1]);last=out;break;
    }
    case 0xe9: {
      require(p.size()==11 && node.links.size()==1 && !(p[10]&~0x33u),"unsupported Layer2D layout/flags");
      if(auto cached=find(id)) {last=*cached;break;}last.reset();children();const auto in=last;
      const auto out=alloc(id,in ? pool[*in].size : p[0],count);if(in)copy(id,*in,out);
      if(node.links[0]) {
        if(p[10]&3)append(id,out,FrameClear{p[10]&3,0xff000000});
        FrameRect xy,uv;for(unsigned i=0;i<4;++i) {xy[i]=real(p,1+i);uv[i]=real(p,5+i);}
        const auto centered=[](FrameRect r) {return FrameRect{r[0]-r[2]*.5f,r[1]-r[3]*.5f,r[0]+r[2]*.5f,r[1]+r[3]*.5f};};
        if(p[10]&0x10)xy=centered(xy);
        if(p[10]&0x20)uv=centered(uv);
        for(const auto& pass:scenes.materials().snapshot(*node.links[0]).passes) {
          FrameQuad q;q.shader=pass.shader;q.rectangle=xy;q.depth=real(p,9);q.uv.fill(uv);
          for(unsigned i=0;i<4;++i)if(auto texture=q.shader.textures[i]) {
            const auto bitmap=concrete(*texture);
            if(project.operators[bitmap].command==0x26)q.textures[i]=FrameTexture{true,bitmap_target(bitmap)};
            else q.textures[i]=FrameTexture{false,*texture};
          }
          append(id,out,std::move(q));
        }
      }
      if(in)release(*in);
      last=out;break;
    }
    case 0xeb: {
      require(p.empty() && node.links.size()==1 && node.links[0],"invalid IPP RenderTarget layout");
      const auto saved=master;master=bitmap_target(*node.links[0]);
      try {children();if(last)copy(id,*last,12);}catch(...) {master=saved;throw;}
      master=saved;break;
    }
    default:throw std::runtime_error("frame operator "+std::to_string(id)+" ("+std::string(operator_info(op.command).name)+") is not ported");
    }
  }
};
FramePlayer::FramePlayer(const Project& p,const Graph& g,const TextureOptions& options):impl(std::make_unique<Impl>(p,g,options)) {}
FramePlayer::~FramePlayer()=default;
DemoFrame FramePlayer::frame(std::size_t root,std::int32_t beat,unsigned width,unsigned height,std::optional<std::int32_t> milliseconds) {
  require(width && height && width<=4096 && height<=4096,"invalid frame dimensions");auto& s=*impl;
  auto initial=frame_environment(0);s.prepare(root,initial);s.result={};s.result.beat=beat;s.result.width=width;s.result.height=height;
  s.result.milliseconds=milliseconds.value_or(0);
  if(!milliseconds)s.result.milliseconds=beat_milliseconds(beat,as_float(s.project.settings[4]));
  s.scenes.begin_frame(s.result.milliseconds);
  s.result.surfaces.emplace(0,FrameSurface{width,height});
  for(unsigned i=0;i<12;++i) {const auto size=i/3;const auto shift=size==3 ? 0 : size;s.result.surfaces.emplace(i+1,FrameSurface{1024u>>shift,512u>>shift});}
  s.master=0;s.reset_pool();s.timeline.begin_frame(beat);auto env=frame_environment(beat);s.execute(root,env,scene_identity());return std::move(s.result);
}
void write_frame(std::ostream& out,const DemoFrame& f) {
  out<<std::setprecision(9)<<"{\"beat\":"<<double(f.beat)/65536<<",\"width\":"<<f.width<<",\"height\":"<<f.height
    <<",\"milliseconds\":"<<f.milliseconds<<",\"visited_operators\":"<<f.visited_operators<<",\"cache_hits\":"<<f.cache_hits<<",\"screen_fallbacks\":"<<f.screen_fallbacks<<",\"surfaces\":{";
  bool first=true;for(const auto& [id,s]:f.surfaces) {out<<(first ? "" : ",")<<'\"'<<id<<"\":["<<s.width<<','<<s.height<<']';first=false;}
  out<<"},\"commands\":[";first=true;
  for(const auto& c:f.commands) {
    out<<(first ? "" : ",")<<"{\"operator\":"<<c.operator_id<<",\"target\":"<<c.target.surface<<",\"viewport\":["<<c.target.width<<','<<c.target.height<<']';first=false;
    if(const auto* scene=std::get_if<SceneFrame>(&c.draw)) {
      std::size_t particles=0;for(const auto& effect:scene->effects)particles+=effect.vertices.size()/4;
      out<<",\"kind\":\"scene\",\"instances\":"<<scene->instances.size()<<",\"lights\":"<<scene->lights.size()<<",\"effect_draws\":"<<scene->effects.size()<<",\"effect_quads\":"<<particles;
    }
    else if(const auto* clear=std::get_if<FrameClear>(&c.draw))out<<",\"kind\":\"clear\",\"flags\":"<<clear->flags<<",\"color\":"<<clear->color;
    else {
      const auto& q=std::get<FrameQuad>(c.draw);out<<",\"kind\":\"quad\",\"rectangle\":[";
      for(unsigned i=0;i<4;++i)out<<(i ? "," : "")<<q.rectangle[i];
      out<<"],\"textures\":[";
      for(unsigned i=0;i<4;++i) {if(i)out<<',';if(q.textures[i])out<<"{\"surface\":"<<(q.textures[i]->surface ? "true" : "false")<<",\"id\":"<<q.textures[i]->id<<'}';else out<<"null";}
      out<<"],\"uv\":[";
      for(unsigned i=0;i<4;++i) {out<<(i ? "," : "")<<'[';for(unsigned j=0;j<4;++j)out<<(j ? "," : "")<<q.uv[i][j];out<<']';}
      out<<"],\"depth\":"<<q.depth<<",\"material\":";Material m;m.passes.push_back({q.shader});write_material(out,m);
    }
    out<<'}';
  }
  out<<"]}\n";require(bool(out),"cannot write frame");
}
} // namespace wz3
