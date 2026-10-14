// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/scene_draw.hpp"
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
SceneDrawList scene_unlit_jobs(const SceneFrame& scene) {
  SceneDrawList result;
  // The old low 16 bits came from a process address. Use deterministic material
  // bindings/pass indices to batch equal shaders without relying on ASLR.
  std::map<std::pair<std::size_t,std::size_t>,unsigned> material_ids;unsigned next=1;
  for(const auto& [id,m]:scene.materials)for(std::size_t p=0;p<m.passes.size();++p) {
    if(next>65535)throw std::runtime_error("scene material sort limit");
    material_ids[{id,p}]=next++;
  }
  for(std::size_t i=0;i<scene.effects.size();++i)result.jobs.push_back({scene.effects[i].sort_key,0,0,0,i});
  // MeshJob is a singly linked list prepended by AddPaintJob.
  for(std::size_t i=scene.instances.size();i--;) {
    const auto& instance=scene.instances[i];if(!instance.mesh)throw std::runtime_error("missing scene geometry");
    auto found=result.meshes.find(instance.mesh.get());
    if(found==result.meshes.end())found=result.meshes.emplace(instance.mesh.get(),scene_mesh_batches(*instance.mesh)).first;
    for(std::size_t b=0;b<found->second.size();++b) {
      const auto& batch=found->second[b];
      if(!batch.material)throw std::runtime_error("default mesh light material is not ported");
      const auto material=scene.materials.find(*batch.material);
      if(material==scene.materials.end())throw std::runtime_error("missing mesh material snapshot");
      if(material->second.material11_insert)throw std::runtime_error("mesh material "+std::to_string(*batch.material)+": lighting insert is not ported");
      for(std::size_t p=0;p<material->second.passes.size();++p) {
        const auto& pass=material->second.passes[p];const auto usage=unsigned(pass.usage);
        if(usage>7)throw std::runtime_error("invalid mesh material usage");
        if(usage==3 || usage==4)throw std::runtime_error("per-light mesh passes are not ported");
        if(pass.program!=MaterialProgram::static_mesh)throw std::runtime_error("mesh finalizer is not ported");
        // Material+face pass wraps first; the instance adjustment then clamps.
        const auto base=(std::int64_t(pass.render_pass)+batch.pass)&255;
        const auto index=std::clamp(base+instance.pass_adjust,std::int64_t(0),std::int64_t(255));
        const auto light=usage<3 ? 0u : 15u;
        const auto key=(std::uint32_t(index)<<24)|(light<<20)|(usage<<16)|material_ids.at({*batch.material,p});
        if(result.jobs.size()>=1000000)throw std::runtime_error("scene paint job limit");
        result.jobs.push_back({key,i,b,p,{}});
      }
    }
  }
  std::stable_sort(result.jobs.begin(),result.jobs.end(),[](const auto& a,const auto& b){return a.sort_key<b.sort_key;});
  return result;
}
} // namespace wz3
