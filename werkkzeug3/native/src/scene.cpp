// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// References: ExecSceneInputs, Exec_Scene_*, Exec_IPP_Viewport and
// sMaterialEnv::MakeProjectionMatrix. No Win32 ABI or graphics API is used here.
#include "wz3/scene.hpp"
#include "wz3/particles.hpp"
#include "wz3/effect_particles.hpp"
#include "wz3/walk.hpp"
#include "wz3/print.hpp"
#include "wz3/spline3d.hpp"
#include "particle_random.hpp"
#include "mesh_kernel.hpp"
#include <bit>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
float real(std::span<const std::uint32_t> p,unsigned i) {
  require(i<p.size(),"scene parameter overrun");const auto v=as_float(p[i]);
  require(std::isfinite(v) && std::abs(v)<=1e15f,"invalid scene numeric parameter");return v;
}
SceneMatrix matrix(const legacy::sMatrix& m) {
  return {m.i.x,m.i.y,m.i.z,m.i.w,m.j.x,m.j.y,m.j.z,m.j.w,m.k.x,m.k.y,m.k.z,m.k.w,m.l.x,m.l.y,m.l.z,m.l.w};
}
Vec3 column(const SceneMatrix& m,unsigned c) {return {m[c*4],m[c*4+1],m[c*4+2]};}
void validate(const SceneMatrix& m) {for(auto v:m)require(std::isfinite(v),"nonfinite scene matrix");}
struct Visit {
  std::vector<bool>& visiting;std::size_t id;
  Visit(std::vector<bool>& v,std::size_t i):visiting(v),id(i) {require(!v[i],"cycle in scene graph");v[i]=true;}
  ~Visit() {visiting[id]=false;}
};
}
SceneMatrix scene_identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
SceneMatrix scene_srt(std::span<const std::uint32_t> p) {
  require(p.size()>=9,"scene SRT needs nine parameters");float values[9];
  for(unsigned i=0;i<9;++i)values[i]=real(p,i);
  legacy::sMatrix m;m.InitSRT(values);return matrix(m);
}
SceneMatrix scene_combine(const SceneMatrix& parent,const SceneMatrix& local) {
  SceneMatrix out{};
  for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)
    out[c*4+r]=parent[r]*local[c*4]+parent[4+r]*local[c*4+1]+parent[8+r]*local[c*4+2]+parent[12+r]*local[c*4+3];
  validate(out);return out;
}
Vec3 scene_point(const SceneMatrix& m,const Vec3& p) {
  Vec3 out{};for(unsigned c=0;c<3;++c)out[c]=m[c]*p[0]+m[4+c]*p[1]+m[8+c]*p[2]+m[12+c];return out;
}
SceneMatrix scene_view_projection(const SceneCamera& camera,unsigned width,unsigned height) {
  require(width && height && width<=4096 && height<=4096,"invalid scene viewport size");validate(camera.world);
  require(std::isfinite(camera.near_clip) && camera.near_clip>0 && std::isfinite(camera.zoom_x) && std::isfinite(camera.zoom_y),"invalid scene projection");
  // CameraSpace.TransR(): the camera transform is rigid, with positive Z forward.
  auto view=scene_identity();
  for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)view[c*4+r]=camera.world[r*4+c];
  for(unsigned r=0;r<3;++r)view[12+r]=-(view[r]*camera.world[12]+view[4+r]*camera.world[13]+view[8+r]*camera.world[14]);
  // Original infinite-far D3D projection, with z_GL = 2*z_D3D - w.
  // The released 0x8034cf projection has zero XY translation; the later archive
  // adds constant half-pixel terms. Keep each engine's geometric matrix.
  const SceneMatrix projection={camera.zoom_x,0,0,0,0,camera.zoom_y,0,0,
    camera.center_x,camera.center_y,1,1,camera.released_projection ? 0 : -1.0f/width,
    camera.released_projection ? 0 : 1.0f/height,-2*camera.near_clip,0};
  return scene_combine(projection,view);
}
SceneMatrix scene_raster_projection(const SceneCamera& camera,unsigned width,unsigned height) {
  auto m=scene_view_projection(camera,width,height);
  if(camera.released_projection)for(unsigned c=0;c<4;++c) {
    // D3D9 samples integer pixel centers; GLES samples half-integers. Move
    // geometry right/down half a pixel after division, independent of depth.
    m[c*4]+=m[c*4+3]/width;m[c*4+1]-=m[c*4+3]/height;
  }
  return m;
}

SceneAssets::SceneAssets(const Project& p,const Graph& g,const TextureOptions& o):project(p),graph(g),options(o) {
  require(g.nodes.size()==p.operators.size(),"scene asset graph size mismatch");
}
bool SceneAssets::matches(const Project& p,const Graph& g,const TextureOptions& o) const {
  return &project==&p && &graph==&g && options.font_file==o.font_file;
}
std::shared_ptr<const Mesh> SceneAssets::mesh(std::size_t id) {
  if(const auto found=meshes.find(id);found!=meshes.end())return found->second;
  auto value=evaluate_mesh(project,graph,id);
  const auto bytes=mesh_storage_bytes(*value);
  require(bytes<=256u*1024*1024-mesh_bytes,"scene mesh cache budget exceeded");
  meshes.emplace(id,value);mesh_bytes+=bytes;return value;
}
std::shared_ptr<const Texture> SceneAssets::font(std::size_t id) {
  if(const auto found=fonts.find(id);found!=fonts.end())return found->second;
  auto value=evaluate_texture(project,graph,id,options);const auto bytes=value->pixels.size()*sizeof(Pixel);
  require(bytes<=64u*1024*1024-font_bytes,"Print texture cache limit");
  fonts.emplace(id,value);font_bytes+=bytes;return value;
}

struct ScenePlayer::Impl {
  const Project& project;const Graph& graph;SplineLibrary splines;MaterialRuntime materials;SpatialSplines spatial;
  std::shared_ptr<ParticleRandom> random=std::make_shared<ParticleRandom>();
  SceneParticles particles;EffectParticles effects;FrameClock clock;SceneWalk walker;
  SceneMarkers markers;
  struct EffectJob {std::size_t id;SceneMatrix world;std::array<AnimationVector,32> variables;std::uint32_t key;};
  std::vector<EffectJob> jobs;
  struct EffectOrder {std::int32_t pass{};unsigned usage=7;};
  std::map<std::size_t,EffectOrder> effect_order;
  std::shared_ptr<SceneAssets> assets;
  std::map<std::uint64_t,std::vector<std::pair<std::size_t,std::size_t>>> instances;
  std::size_t instance_count{};
  std::vector<bool> prepared;
  std::vector<std::optional<AnimationProgram>> programs;
  std::vector<bool> visiting;
  Impl(const Project& p,const Graph& g,const TextureOptions& options,std::shared_ptr<SceneAssets> shared):project(p),graph(g),splines(p),materials(p,g),spatial(p,g),particles(splines,random,p.runtime_graph),effects(random),walker(splines),assets(shared ? std::move(shared) : std::make_shared<SceneAssets>(p,g,options)),prepared(p.operators.size()),programs(p.operators.size()),visiting(programs.size()) {
    require(graph.nodes.size()==project.operators.size(),"scene graph size mismatch");
    require(assets->matches(p,g,options),"scene assets belong to a different project, graph or font");
    markers.fill(scene_identity());
  }
  void valid(std::size_t id) const {require(id<programs.size() && graph.nodes[id].active,"invalid or inactive scene operator");}
  std::size_t alias(std::size_t id) const {
    valid(id);const auto cmd=project.operators[id].command;const auto& node=graph.nodes[id];
    if(cmd==1) {require(node.links.size()==1 && node.links[0].has_value(),"unresolved scene Load");return *node.links[0];}
    require((cmd==2 || cmd==3 || cmd==5 || cmd==6 || cmd==0x16 || cmd==0x180) && node.inputs.size()==1,"invalid scene alias");return node.inputs[0];
  }
  std::size_t concrete(std::size_t id,bool follow_scene_pass=true) const {
    for(unsigned depth=0;depth<512;++depth) {
      valid(id);const auto cmd=project.operators[id].command;
      if(cmd==0x180 && !follow_scene_pass)return id;
      if((cmd<1 || cmd>3) && cmd!=5 && cmd!=6 && cmd!=0x16 && cmd!=0x180)return id;
      id=alias(id);
    }
    throw std::runtime_error("cycle or excessive depth in scene aliases");
  }
  const AnimationProgram& program(std::size_t id) {
    if(!programs[id])programs[id]=compile_animation(project.operators[id],splines);
    return *programs[id];
  }
  std::shared_ptr<const Mesh> geometry(std::size_t id) {
    return assets->mesh(id);
  }
  std::size_t instance(std::size_t id,AnimationEnvironment& env) {
    auto& cursor=*env.instances;auto& chain=instances[cursor.chain];
    require(cursor.next<=chain.size(),"invalid particle instance chain cursor");
    if(cursor.next==chain.size()) {require(instance_count<65536,"particle instance memory limit");chain.emplace_back(id,instance_count++);}
    const auto [owner,token]=chain[cursor.next++];require(owner==id,"particle instance chain changed operator order");return token;
  }
  void prepare_effect(std::size_t id,std::span<const std::uint32_t> p) {
    valid(id);const auto cmd=project.operators[id].command;if((cmd!=0x64 && cmd!=0x61) || effect_order.contains(id))return;
    require((cmd==0x64 ? p.size()==12 : (p.size()==5 || p.size()==6)) && graph.nodes[id].inputs.empty() && graph.nodes[id].links.size()==1,"unsupported particle/Print effect layout");
    std::int32_t pass=0;unsigned usage=7;
    if(const auto material=graph.nodes[id].links[0]) {const auto m=materials.snapshot(*material);if(!m.passes.empty()) {pass=m.passes[0].render_pass;if(cmd==0x61)usage=unsigned(m.passes[0].usage);}}
    if(cmd==0x64)effects.initialize(p[0],p[1]);
    effect_order.emplace(id,EffectOrder{pass,usage});
  }
  std::shared_ptr<const Texture> print_texture(std::size_t id) {
    return assets->font(id);
  }
  void prepare(std::size_t id,AnimationEnvironment& env,unsigned depth=0) {
    valid(id);if(prepared[id])return;require(depth<512,"scene preparation depth limit");
    if(operator_info(project.operators[concrete(id)].command).category=="MATERIAL") {materials.precalculate(id,env);prepared[id]=true;return;}
    if(operator_info(project.operators[concrete(id)].command).category=="SPLINE") {spatial.precalculate(id,env);prepared[id]=true;return;}
    Visit visit(visiting,id);
    const auto& op=project.operators[id];const AnimationScope scope(program(id),splines,op.parameters,env);
    {
      for(auto child:graph.nodes[id].inputs)prepare(child,env,depth+1);
      for(auto child:graph.nodes[id].links)if(child)prepare(*child,env,depth+1);
      prepare_effect(id,scope.parameters);
    }
    prepared[id]=true;
  }
  void effect(std::size_t id,AnimationEnvironment& env,const SceneMatrix& world,SceneFrame& frame,std::uint32_t key,unsigned depth=0) {
    valid(id);require(depth<512,"effect alias depth limit");Visit visit(visiting,id);
    const auto& op=project.operators[id];const auto& node=graph.nodes[id];const AnimationScope scope(program(id),splines,op.parameters,env);
    if(op.command>=1 && op.command<=3) {effect(alias(id),env,world,frame,key,depth+1);return;}
    require(op.command==0x64 || op.command==0x61,"effect alias execution is not ported");
    // The handler reads its first cached material pass; it does not execute the
    // material link. Later MatHack operations have already run at this point.
    if(node.links[0]) {const auto material=materials.snapshot(*node.links[0]);if(!material.passes.empty()) {
      SceneEffectDraw draw;draw.operator_id=id;draw.sort_key=key;draw.shader=material.passes[0].shader;
      if(op.command==0x64)draw.vertices=effects.draw(scope.parameters,frame.camera,world,clock);
      else {
        require(op.strings.size()==1,"Print needs one text string");
        const auto page=(scope.parameters[0]>>4)&7;require(page!=0,"Print built-in font page zero is not ported");
        std::shared_ptr<const FontAtlas> atlas;
        // Theta stores its only font page in this material's texture recipe.
        // Ambiguous pages fail rather than depending on incidental map order.
        for(auto texture:draw.shader.textures)if(texture) {
          const auto bitmap=print_texture(*texture);draw.textures.emplace(*texture,bitmap);
          if(bitmap->fonts[page]) {require(!atlas || atlas==bitmap->fonts[page],"ambiguous Print font page");atlas=bitmap->fonts[page];}
        }
        require(atlas!=nullptr,"Print material has no matching font atlas page");
        auto layout=layout_print(scope.parameters,op.strings[0],atlas->glyphs,project.runtime_graph);draw.normalized=layout.normalized;draw.vertices=std::move(layout.vertices);
        if(!draw.normalized)for(auto& vertex:draw.vertices)vertex.position=scene_point(world,vertex.position);
      }
      frame.effects.push_back(std::move(draw));
    }}
  }
  template<class F> void dispatch(std::size_t id,std::span<const std::uint32_t> p,AnimationEnvironment& env,
                                  const SceneMatrix& world,EventTimeline* timeline,F child) {
    const auto cmd=project.operators[id].command;
    require(timeline!=nullptr,"scene Event/Time needs a prepared timeline");
    require(graph.nodes[id].inputs.size()==1,"event operator needs one input");
    require((cmd==6 && p.size()==1) || (cmd==5 && p.empty()),"unsupported event layout");
    for(auto event_id:timeline->active(id)) {
      const auto& event=cmd==6 ? timeline->start(event_id,real(p,0)) : timeline->event(event_id);
      const EventAnimationScope scope(event,timeline->beat(),splines,env,event_id);
      child(graph.nodes[id].inputs[0],scene_combine(world,scene_srt(event.transform)));
      if(cmd==5)break; // Time executes only FirstEvent; Event executes the list.
    }
  }
  void execute(std::size_t id,AnimationEnvironment& env,const SceneMatrix& world,SceneFrame& frame,EventTimeline* timeline,unsigned depth=0,std::int32_t pass=0) {
    valid(id);require(depth<512 && ++frame.visited_operators<=1000000,"scene traversal limit");
    // AdjustPass produces a scene even when its input is a mesh. It must run
    // before ExecSceneInput-style dispatch reaches the cached geometry.
    Visit visit(visiting,id);const auto target=concrete(id,false);
    const auto category=operator_info(project.operators[target].command).category;
    if(category=="MESH" || category=="MINMESH") {
      require(frame.instances.size()<65536,"scene instance limit");
      for(auto source=id;source!=target;source=alias(source))
        require(!project.operators[source].has_animation(),"animated mesh alias precalculation is not ported");
      // ExecSceneInput emits cached geometry without calling mesh Exec handlers.
      frame.instances.push_back({id,geometry(target),world,env.variables[0][0],pass});return;
    }
    if(operator_info(project.operators[target].command).category=="EFFECT") {
      if((project.operators[target].command!=0x64 && project.operators[target].command!=0x61))throw std::runtime_error("scene effect "+std::to_string(target)+" ("+std::string(operator_info(project.operators[target].command).name)+") is not ported");
      require(jobs.size()<65536,"effect job limit");
      const auto key=effect_order.at(target);const auto render_pass=std::clamp(std::int64_t(key.pass)+pass,std::int64_t(0),std::int64_t(255));
      jobs.push_back({id,world,env.variables,(std::uint32_t(render_pass)<<24)|(key.usage<<16)});return;
    }
    const auto& op=project.operators[id];const auto& node=graph.nodes[id];
    const AnimationScope animation(program(id),splines,op.parameters,env);const auto& p=animation.parameters;
    if(op.command>=1 && op.command<=3) {execute(alias(id),env,world,frame,timeline,depth+1,pass);return;}
    const auto one=[&](){require(node.inputs.size()==1,"scene operator needs one input");return node.inputs[0];};
    const auto children=[&](const SceneMatrix& transform,std::int32_t adjust) {for(auto input:node.inputs)execute(input,env,transform,frame,timeline,depth+1,adjust);};
    switch(op.command) {
    case 5:case 6:
      dispatch(id,p,env,world,timeline,[&](auto input,const auto& transform){execute(input,env,transform,frame,timeline,depth+1,pass);});break;
    case 0xd2:
      require(p.empty() && node.inputs.empty() && node.links.size()==1,"unsupported MatHack layout");
      if(node.links[0])materials.execute(*node.links[0],env);
      break;
    case 0xc0:case 0xc3:
      require(p.size()==(op.command==0xc0 ? 10u : 9u),"unsupported scene transform layout");
      execute(one(),env,scene_combine(world,scene_srt(p)),frame,timeline,depth+1,pass);break;
    case 0xc1:require(p.empty(),"unsupported scene Add layout");children(world,pass);break;
    case 0xc2: {
      require(p.size()==10 && p[9]<=4096,"unsupported scene Multiply layout/count");const auto input=one();const auto step=scene_srt(p);auto transform=world;
      const auto selection=env.variables[3];
      try {for(unsigned i=0;i<p[9];++i) {env.variables[3].fill(float(i));execute(input,env,transform,frame,timeline,depth+1,pass);transform=scene_combine(transform,step);}}
      catch(...) {env.variables[3]=selection;throw;}
      env.variables[3]=selection;break;
    }
    case 0xc4: {
      require(p.size()==11 && node.inputs.empty(),"unsupported scene Light layout");
      std::array<std::uint32_t,9> srt={std::bit_cast<std::uint32_t>(1.0f),std::bit_cast<std::uint32_t>(1.0f),std::bit_cast<std::uint32_t>(1.0f),p[0],p[1],p[2],p[3],p[4],p[5]};
      const auto transform=scene_combine(world,scene_srt(srt));const auto range=real(p,10);
      SceneLight light{id,column(transform,3),column(transform,2),p[6],p[8],real(p,9),range ? range : 1e15f};
      if(!range && (p[6]&4))for(unsigned c=0;c<3;++c)light.position[c]=light.direction[c]*1e6f;
      require(frame.lights.size()<4096,"scene light limit");frame.lights.push_back(light);break;
    }
    case 0x63:
      require(node.inputs.empty(),"PartEmitter mesh input is not ported");
      effects.emit(instance(id,env),p,world,clock);break;
    case 0xc5: {
      require(node.inputs.size()==1,"Particles with a GenSpline input is not ported");
      require(op.splines.size()<=1,"unsupported Particles spline layout");std::optional<std::size_t> spline;
      if(!op.splines.empty() && !op.splines[0].empty()) {spline=splines.find(op.splines[0]);require(spline.has_value(),"unresolved particle spline");}
      const auto instances=particles.evaluate(instance(id,env),p,spline);const auto selection=env.variables[3],fraction=env.variables[11];
      try {for(const auto& particle:instances) {
        // The released handler predates KV_SELECT/KV_FRACTION publication.
        if(!project.runtime_graph) {env.variables[3].fill(float(particle.index));env.variables[11].fill(particle.fraction);}
        execute(node.inputs[0],env,scene_combine(world,particle.transform),frame,timeline,depth+1,pass);
      }}catch(...) {env.variables[3]=selection;env.variables[11]=fraction;throw;}
      env.variables[3]=selection;env.variables[11]=fraction;break;
    }
    case 0xc7: {
      require(node.inputs.size()==2,"Limb needs two scene inputs");const auto pose=scene_limb(p,world);
      const std::array<AnimationVector,4> saved={env.variables[28],env.variables[29],env.variables[30],env.variables[31]};
      try {for(unsigned i=0;i<2;++i) {for(unsigned c=0;c<4;++c)std::copy_n(pose.other[i].begin()+c*4,4,env.variables[28+c].begin());execute(node.inputs[i],env,pose.world[i],frame,timeline,depth+1,pass);}}
      catch(...) {std::copy(saved.begin(),saved.end(),env.variables.begin()+28);throw;}
      std::copy(saved.begin(),saved.end(),env.variables.begin()+28);break;
    }
    case 0xc8: {
      require(node.inputs.size()==1 && op.splines.size()<=1,"unsupported Walk input/spline layout");std::optional<std::size_t> spline;
      if(!op.splines.empty() && !op.splines[0].empty()) {spline=splines.find(op.splines[0]);require(spline.has_value(),"unresolved Walk step spline");}
      const auto pose=walker.evaluate(instance(id,env),p,world,clock,spline);std::array<AnimationVector,8> saved;
      std::copy_n(env.variables.begin()+16,8,saved.begin());const auto timing=env.variables[27];
      std::copy_n(pose.feet.begin(),pose.count,env.variables.begin()+16);env.variables[27]=pose.timing;
      try {execute(node.inputs[0],env,pose.body,frame,timeline,depth+1,pass);}
      catch(...) {std::copy(saved.begin(),saved.end(),env.variables.begin()+16);env.variables[27]=timing;throw;}
      std::copy(saved.begin(),saved.end(),env.variables.begin()+16);env.variables[27]=timing;break;
    }
    case 0xc9:execute(one(),env,scene_rotate(p,world,frame.camera),frame,timeline,depth+1,pass);break;
    case 0x180: {
      require(p.size()==1,"unsupported AdjustPass layout");const auto adjust=std::int64_t(pass)+std::bit_cast<std::int32_t>(p[0]);
      require(adjust>=-2147483648LL && adjust<=2147483647LL,"scene pass overflow");execute(one(),env,world,frame,timeline,depth+1,std::int32_t(adjust));break;
    }
    case 0x181: {
      require(p.size()==1 && node.inputs.size()==2,"Apply Spline needs a scene, a spline and a time");
      const auto pose=spatial.evaluate(node.inputs[1],real(p,0),0,env,timeline);
      execute(node.inputs[0],env,scene_combine(world,pose.world),frame,timeline,depth+1,pass);break;
    }
    case 0x182:
      require(p.size()==1 && p[0]<markers.size() && node.inputs.size()<=1,"invalid scene Marker");
      markers[p[0]]=world;children(world,pass);break;
    case 0x184:require(p.size()==1 && node.inputs.empty(),"unsupported Ambient layout");frame.ambient=p[0];break;
    case 0x185: {
      require(p.size()==1,"unsupported scene Select layout");if(node.inputs.empty())break;
      const double value=std::nearbyint(real(p,0));require(value>=0 && value<=2147483647,"invalid scene selection");
      execute(node.inputs[std::size_t(value)%node.inputs.size()],env,world,frame,timeline,depth+1,pass);break;
    }
    default:throw std::runtime_error("scene operator "+std::to_string(id)+" ("+std::string(operator_info(op.command).name)+") is not ported");
    }
  }
  SceneFrame viewport(std::size_t id,AnimationEnvironment& env,float aspect,const SceneMatrix& world,EventTimeline* timeline,unsigned depth=0) {
    valid(id);require(depth<512,"viewport alias depth limit");Visit visit(visiting,id);
    const auto& op=project.operators[id];const auto& node=graph.nodes[id];
    const AnimationScope animation(program(id),splines,op.parameters,env);const auto& p=animation.parameters;
    if(op.command>=1 && op.command<=3)return viewport(alias(id),env,aspect,world,timeline,depth+1);
    require(op.command==0xf0 && (p.size()==18 || p.size()==24),"expected a supported Viewport operator");
    require(node.inputs.size()==1 || node.inputs.size()==2,"invalid viewport input count");
    require(!(p[1]&~0x1033u),"viewport camera/stereo flags are not ported");
    if(p.size()==24)require(real(p,20)==0 && real(p,21)==0 && real(p,22)==1 && real(p,23)==1,"viewport cropping is not ported");
    SceneFrame frame;frame.viewport_id=id;frame.viewport_flags=p[1];frame.background=p[2];
    std::array<std::uint32_t,9> srt={std::bit_cast<std::uint32_t>(1.0f),std::bit_cast<std::uint32_t>(1.0f),std::bit_cast<std::uint32_t>(1.0f),p[3],p[4],p[5],p[6],p[7],p[8]};
    auto& camera=frame.camera;camera.released_projection=project.runtime_graph;
    camera.world=scene_srt(srt);camera.far_clip=real(p,9);camera.near_clip=real(p,10);
    camera.center_x=real(p,11);camera.center_y=real(p,12);camera.zoom_x=real(p,13);camera.zoom_y=real(p,14)*((p[1]&0x1000) ? 1 : aspect);
    camera.fog_color=p[15];camera.fog_end=real(p,16);camera.fog_start=real(p,17);
    if(node.inputs.size()==2) {
      const auto pose=spatial.evaluate(node.inputs[1],env.variables[0][0],0,env,timeline);
      camera.world=pose.world;camera.zoom_x*=pose.zoom;camera.zoom_y*=pose.zoom;
    }
    require(camera.near_clip>0 && camera.far_clip>camera.near_clip && camera.zoom_x>0 && camera.zoom_y>0,"invalid saved scene camera");
    jobs.clear();execute(node.inputs[0],env,world,frame,timeline);
    // Engine prepends effect jobs, then performs a stable sort by pass/usage.
    std::reverse(jobs.begin(),jobs.end());std::stable_sort(jobs.begin(),jobs.end(),[](const auto& a,const auto& b){return a.key<b.key;});
    const auto saved_variables=env.variables;const auto saved_instances=env.instances;
    try {for(const auto& job:jobs) {
      env.variables=job.variables; // EventSpline deliberately stays at paint scope.
      env.instances=std::make_shared<AnimationInstanceCursor>(AnimationInstanceCursor{(std::uint64_t(1)<<63)|job.id,0});
      effect(job.id,env,job.world,frame,job.key);
    }}catch(...) {env.variables=saved_variables;env.instances=saved_instances;throw;}
    env.variables=saved_variables;env.instances=saved_instances;

    // Engine painting consumes material state after all scene jobs have been
    // collected, including updates executed later in the same scene traversal.
    for(const auto& instance:frame.instances)for(const auto& face:instance.mesh->faces)
      if(face.material && !frame.materials.contains(*face.material))frame.materials.emplace(*face.material,materials.snapshot(*face.material));
    if(frame.lights.empty())frame.lights.push_back({id,column(camera.world,3),{0,0,1},0,0x78787878,1,32});
    return frame;
  }
  void event_viewports(std::size_t id,AnimationEnvironment& env,const SceneMatrix& world,EventTimeline& timeline,
                       float aspect,std::vector<SceneFrame>& frames,std::size_t& visits,unsigned depth=0) {
    valid(id);require(depth<512 && frames.size()<4096 && ++visits<=1000000,"event viewport traversal limit");
    const auto& op=project.operators[id];
    if(op.command==0xf0) {frames.push_back(viewport(id,env,aspect,world,&timeline));return;}
    Visit visit(visiting,id);const AnimationScope scope(program(id),splines,op.parameters,env);
    if(op.command>=1 && op.command<=3)event_viewports(alias(id),env,world,timeline,aspect,frames,visits,depth+1);
    else if(op.command==5 || op.command==6)
      dispatch(id,scope.parameters,env,world,&timeline,[&](auto input,const auto& transform){event_viewports(input,env,transform,timeline,aspect,frames,visits,depth+1);});
    else throw std::runtime_error("event viewport path contains unsupported operator "+std::to_string(id)+" ("+std::string(operator_info(op.command).name)+")");
  }
};
ScenePlayer::ScenePlayer(const Project& p,const Graph& g,const TextureOptions& options,std::shared_ptr<SceneAssets> assets):impl(std::make_unique<Impl>(p,g,options,std::move(assets))) {}
ScenePlayer::~ScenePlayer()=default;
MaterialRuntime& ScenePlayer::materials() {return impl->materials;}
const SceneMarkers& ScenePlayer::markers() const {return impl->markers;}
void ScenePlayer::begin_frame(std::int32_t milliseconds) {impl->clock.advance(milliseconds);}
void ScenePlayer::prepare_effect(std::size_t id,std::span<const std::uint32_t> p) {impl->prepare_effect(id,p);}
SceneFrame ScenePlayer::viewport(std::size_t id,AnimationEnvironment env,float aspect,const SceneMatrix& world,EventTimeline* timeline) {
  require(std::isfinite(aspect) && aspect>0 && aspect<=16,"invalid scene aspect");validate(world);auto initial=frame_environment(0);impl->prepare(id,initial);return impl->viewport(id,env,aspect,world,timeline);
}
std::vector<SceneFrame> ScenePlayer::event_viewports(std::size_t id,EventTimeline& timeline,float aspect) {
  require(std::isfinite(aspect) && aspect>0 && aspect<=16,"invalid scene aspect");
  auto env=frame_environment(timeline.beat());std::vector<SceneFrame> frames;std::size_t visits=0;
  auto initial=frame_environment(0);impl->prepare(id,initial);
  impl->event_viewports(id,env,scene_identity(),timeline,aspect,frames,visits);return frames;
}
Bounds scene_bounds(const SceneFrame& frame) {
  Bounds bounds{};bool first=true;
  for(const auto& instance:frame.instances) {
    require(instance.mesh!=nullptr,"missing scene mesh");const auto box=mesh_bounds(*instance.mesh);
    for(unsigned i=0;i<8;++i) {
      Vec3 point{};for(unsigned c=0;c<3;++c)point[c]=(i&(1u<<c)) ? box.max[c] : box.min[c];point=scene_point(instance.world,point);
      if(first) {bounds={point,point};first=false;}
      for(unsigned c=0;c<3;++c) {bounds.min[c]=std::min(bounds.min[c],point[c]);bounds.max[c]=std::max(bounds.max[c],point[c]);}
    }
  }
  require(!first,"empty scene has no bounds");return bounds;
}
void write_scene(const std::filesystem::path& path,const SceneFrame& frame) {
  std::ofstream out(path);require(bool(out),"cannot open scene output");out<<std::setprecision(9);
  const auto array=[&](const auto& values){out<<'[';bool first=true;for(auto v:values){out<<(first ? "" : ",")<<v;first=false;}out<<']';};
  out<<"{\"viewport\":"<<frame.viewport_id<<",\"visited_operators\":"<<frame.visited_operators
    <<",\"viewport_flags\":"<<frame.viewport_flags<<",\"background\":"<<frame.background<<",\"ambient\":"<<frame.ambient<<",\"camera\":{";
  out<<"\"world\":";array(frame.camera.world);out<<",\"near\":"<<frame.camera.near_clip<<",\"far\":"<<frame.camera.far_clip
    <<",\"center\":["<<frame.camera.center_x<<','<<frame.camera.center_y<<"],\"zoom\":["<<frame.camera.zoom_x<<','<<frame.camera.zoom_y
    <<"],\"released_projection\":"<<(frame.camera.released_projection ? "true" : "false")
    <<",\"fog_color\":"<<frame.camera.fog_color<<",\"fog_start\":"<<frame.camera.fog_start<<",\"fog_end\":"<<frame.camera.fog_end<<"},\"instances\":[";
  for(std::size_t i=0;i<frame.instances.size();++i) {
    const auto& v=frame.instances[i];out<<(i ? "," : "")<<"{\"operator\":"<<v.operator_id<<",\"world\":";array(v.world);
    out<<",\"time\":"<<v.time<<",\"pass_adjust\":"<<v.pass_adjust<<",\"vertices\":"<<v.mesh->vertices.size()<<",\"faces\":"<<v.mesh->faces.size()<<'}';
  }
  out<<"],\"lights\":[";
  for(std::size_t i=0;i<frame.lights.size();++i) {
    const auto& v=frame.lights[i];out<<(i ? "," : "")<<"{\"operator\":"<<v.operator_id<<",\"position\":";array(v.position);
    out<<",\"direction\":";array(v.direction);out<<",\"flags\":"<<v.flags<<",\"color\":"<<v.color<<",\"amplify\":"<<v.amplify<<",\"range\":"<<v.range<<'}';
  }
  out<<"],\"effects\":[";
  for(std::size_t i=0;i<frame.effects.size();++i) {const auto& e=frame.effects[i];out<<(i ? "," : "")<<"{\"operator\":"<<e.operator_id<<",\"sort_key\":"<<e.sort_key<<",\"quads\":"<<e.vertices.size()/4<<'}';}
  out<<"],\"materials\":{";bool first_material=true;
  for(const auto& [id,material]:frame.materials) {out<<(first_material ? "" : ",")<<'\"'<<id<<"\":";write_material(out,material);first_material=false;}
  out<<"}}\n";require(bool(out),"cannot write scene output");
}
} // namespace wz3
