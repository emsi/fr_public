// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/scene_draw.hpp"
#include "wz3/lighting.hpp"
#include <algorithm>
#include <cfenv>
#include <cmath>
namespace wz3 {
std::vector<SceneMeshBatch> scene_mesh_batches(const Mesh& mesh) {
  validate_mesh(mesh);
  std::map<std::pair<std::optional<std::size_t>,std::int32_t>,std::size_t> groups;
  std::vector<SceneMeshBatch> result;
  for(std::size_t i=0;i<mesh.faces.size();++i) {
    const auto& f=mesh.faces[i];auto [found,added]=groups.try_emplace({f.material,f.pass},result.size());
    if(added)result.push_back({f.material,f.pass,{}});
    result[found->second].faces.push_back(std::uint32_t(i));
  }
  return result;
}
std::uint32_t mesh_vertex_color(const std::array<float,4>& color) {
  const auto previous=std::fegetround();std::fesetround(FE_TONEAREST);
  std::uint32_t result=0;
  for(unsigned c=0;c<4;++c) {
    if(!std::isfinite(color[c])) {std::fesetround(previous);throw std::runtime_error("nonfinite mesh vertex color");}
    const auto value=std::uint32_t(std::nearbyint(std::clamp(color[c],0.f,1.f)*255.f));
    result|=value<<(c==0 ? 16 : c==1 ? 8 : c==2 ? 0 : 24);
  }
  std::fesetround(previous);return result;
}
namespace {
using Plane=std::array<float,4>;
std::array<Plane,5> frustum(const SceneCamera& camera,unsigned width,unsigned height,std::array<float,4> rect={-1,-1,1,1},bool normalize=true) {
  const auto m=scene_view_projection(camera,width,height);std::array<Plane,5> planes{};
  for(unsigned c=0;c<4;++c) {planes[0][c]=-rect[0]*m[c*4+3]+m[c*4];planes[1][c]=rect[2]*m[c*4+3]-m[c*4];planes[2][c]=-rect[1]*m[c*4+3]+m[c*4+1];planes[3][c]=rect[3]*m[c*4+3]-m[c*4+1];planes[4][c]=(m[c*4+3]+m[c*4+2])*.5f;}
  if(normalize)for(auto& p:planes){const auto n=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);for(auto& c:p)c/=n;}
  return planes;
}
float distance(const Plane& p,const Vec3& v){return p[0]*v[0]+p[1]*v[1]+p[2]*v[2]+p[3];}
Bounds batch_bounds(const SceneInstance& instance,const SceneMeshBatch& batch) {
  const auto& mesh=*instance.mesh;const auto first=mesh.vertices[mesh.faces[batch.faces[0]].vertices[0]].position;Bounds local{first,first};
  for(auto f:batch.faces)for(unsigned i=0;i<mesh.faces[f].count;++i)for(unsigned c=0;c<3;++c){const auto v=mesh.vertices[mesh.faces[f].vertices[i]].position[c];local.min[c]=std::min(local.min[c],v);local.max[c]=std::max(local.max[c],v);}
  auto p=scene_point(instance.world,local.min);Bounds out{p,p};
  for(unsigned i=1;i<8;++i){Vec3 v;for(unsigned c=0;c<3;++c)v[c]=(i&(1<<c)) ? local.max[c] : local.min[c];v=scene_point(instance.world,v);for(unsigned c=0;c<3;++c){out.min[c]=std::min(out.min[c],v[c]);out.max[c]=std::max(out.max[c],v[c]);}}return out;
}
bool intersects(const Bounds& box,const SceneLight& light) {
  float d=0;for(unsigned c=0;c<3;++c){const auto v=light.position[c]-std::clamp(light.position[c],box.min[c],box.max[c]);d+=v*v;}return d<=light.range*light.range;
}
}
std::vector<ScenePaintLight> scene_lights(const SceneFrame& scene,unsigned width,unsigned height) {
  const auto planes=frustum(scene.camera,width,height);std::vector<std::pair<float,ScenePaintLight>> chosen;
  for(const auto& light:scene.lights) {
    if(!std::isfinite(light.range) || light.range<=0 || !std::isfinite(light.amplify))throw std::runtime_error("invalid scene light");
    for(auto c:light.position)if(!std::isfinite(c))throw std::runtime_error("invalid light position");
    if(light.flags&1)throw std::runtime_error("weapon light priority is not ported");
    if(light.amplify<1.f/255 || std::any_of(planes.begin(),planes.end(),[&](const auto& p){return distance(p,light.position)<-light.range;}))continue;
    auto c=light_model_vector(scene.camera.world,light.position,true);std::array<float,4> rect={-1,-1,1,1};const float zoom[2]={scene.camera.zoom_x,scene.camera.zoom_y};
    for(unsigned i=0;i<2;++i){const auto ds=c[i]*c[i]+c[2]*c[2],q=ds-light.range*light.range;if(q>0){const auto a=std::sqrt(q);auto s=c[i]*a-c[2]*light.range,t=c[i]*light.range+c[2]*a;if(c[2]*ds>-light.range*s)rect[i]=std::max(-1.f,s/t*zoom[i]);s=c[2]*light.range+c[i]*a;t=c[2]*a-c[i]*light.range;if(c[2]*ds>light.range*s)rect[i+2]=std::min(1.f,s/t*zoom[i]);}}
    if(rect[0]>=rect[2] || rect[1]>=rect[3])continue;
    const auto d=std::sqrt(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]);chosen.push_back({light.range/std::max(d,.1f),{light,rect}});
  }
  std::stable_sort(chosen.begin(),chosen.end(),[](const auto& a,const auto& b){return a.first>b.first;});if(chosen.size()>16)chosen.resize(16);
  std::vector<ScenePaintLight> result;for(auto& item:chosen)result.push_back(item.second);
  if(result.empty()) {
    SceneLight fallback{scene.viewport_id,{scene.camera.world[12],scene.camera.world[13],scene.camera.world[14]},{0,0,1},0,0x78787878,1,32};
    if(std::none_of(planes.begin(),planes.end(),[&](const auto& p){return distance(p,fallback.position)<-fallback.range;}))result.push_back({fallback,{-1,-1,1,1}});
  }
  for(auto& l:result) {
    l.shadow_frustum=frustum(scene.camera,width,height,l.rectangle,false);
    for(auto& p:l.shadow_frustum){const auto d=distance(p,l.light.position);if(d<0)p[3]-=d;}
    l.near_volume=shadow_near_volume(scene.camera,l.light.position,l.rectangle);
  }
  return result;
}
const Material& scene_default_material() {
  static const Material material=[] {Material m;MaterialPass p;p.usage=MaterialUsage::light;auto& w=p.shader.words;w[0]=0x300;w[2]=5;w[8]=0x10;w[9]=0x40;w[28]=0x00c0c0c0;w[29]=0x00404040;w[32]=std::bit_cast<std::uint32_t>(32.f);m.passes.push_back(p);return m;}();return material;
}
SceneDrawList scene_draw_jobs(const SceneFrame& scene,unsigned width,unsigned height) {
  SceneDrawList result;result.lights=scene_lights(scene,width,height);const auto planes=frustum(scene.camera,width,height);
  std::map<std::pair<std::size_t,std::size_t>,unsigned> material_ids;unsigned next=1;
  for(const auto& [id,m]:scene.materials)for(std::size_t p=0;p<m.passes.size();++p) {
    if(next>65535)throw std::runtime_error("scene material sort limit");
    material_ids[{id,p}]=next++;
  }
  for(std::size_t i=0;i<scene.effects.size();++i)result.jobs.push_back({scene.effects[i].sort_key,0,0,0,i,{}});
  const auto mode=(scene.viewport_flags>>4)&3;if(mode==3)throw std::runtime_error("invalid scene paint mode");
  for(std::size_t i=scene.instances.size();i--;) {
    const auto& instance=scene.instances[i];if(!instance.mesh)throw std::runtime_error("missing scene geometry");
    auto found=result.meshes.find(instance.mesh.get());
    if(found==result.meshes.end())found=result.meshes.emplace(instance.mesh.get(),scene_mesh_batches(*instance.mesh)).first;
    std::optional<std::size_t> first_shadow;
    SceneMeshBatch whole;
    for(std::size_t b=0;b<found->second.size();++b) {
      const auto& batch=found->second[b];whole.faces.insert(whole.faces.end(),batch.faces.begin(),batch.faces.end());
      if(batch.material && !scene.materials.contains(*batch.material))throw std::runtime_error("missing mesh material snapshot");
      const auto& m=batch.material ? scene.materials.at(*batch.material) : scene_default_material();
      if(!first_shadow && std::any_of(m.passes.begin(),m.passes.end(),[](const auto& p){return p.program==MaterialProgram::shadow;}))first_shadow=b;
    }
    for(std::size_t b=0;b<found->second.size();++b) {
      const auto& batch=found->second[b];
      if(batch.material && !scene.materials.contains(*batch.material))throw std::runtime_error("missing mesh material snapshot");
      const auto& material=batch.material ? scene.materials.at(*batch.material) : scene_default_material();const auto batch_box=batch_bounds(instance,batch);
      for(std::size_t p=0;p<material.passes.size();++p) {
        const auto& pass=material.passes[p];const auto usage=unsigned(pass.usage);
        if(usage>7)throw std::runtime_error("invalid mesh material usage");
        if(mode==1 && usage!=7)continue;
        const bool shadow=pass.program==MaterialProgram::shadow;
        if(shadow && usage!=3)throw std::runtime_error("shadow finalizer requires shadow usage");
        if(shadow && b!=first_shadow)continue;
        const auto box=shadow ? batch_bounds(instance,whole) : batch_box;
        if(usage!=3 && shadow_box_culled(box,planes))continue;
        const auto base=(std::int64_t(pass.render_pass)+batch.pass)&255;
        const auto index=std::clamp(base+instance.pass_adjust,std::int64_t(0),std::int64_t(255));
        if(material.material11_insert)result.inserts[index]=true;
        if(material.material20_insert)result.material20_inserts[index]=true;
        const auto append=[&](unsigned slot,std::optional<std::size_t> light,bool zfail=false){
          const auto key=(std::uint32_t(index)<<24)|(slot<<20)|(usage<<16)|(batch.material ? material_ids.at({*batch.material,p}) : 0);
          if(result.jobs.size()>=1000000)throw std::runtime_error("scene paint job limit");
          result.jobs.push_back({key,i,b,p,{},light,zfail});
        };
        if(usage==3 || usage==4) {
          for(std::size_t l=0;l<result.lights.size();++l) {
            const auto& light=result.lights[l].light;if(!intersects(box,light))continue;
            if(usage==3) {
              if(!(light.flags&2) || shadow_box_culled(box,result.lights[l].shadow_frustum))continue;
              if(!shadow)throw std::runtime_error("shadow usage requires the shadow finalizer");
            }else if(pass.program!=MaterialProgram::static_mesh)throw std::runtime_error("mesh finalizer is not ported");
            append(unsigned(l),l,shadow && !shadow_box_culled(box,result.lights[l].near_volume));
          }
        }else {if(pass.program!=MaterialProgram::static_mesh)throw std::runtime_error("mesh finalizer is not ported");append(usage<3 ? 0u : 15u,{});}
      }
    }
  }
  std::stable_sort(result.jobs.begin(),result.jobs.end(),[](const auto& a,const auto& b){return a.sort_key<b.sort_key;});return result;
}
} // namespace wz3
