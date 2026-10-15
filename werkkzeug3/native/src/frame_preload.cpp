// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/frame_preload.hpp"
#include "wz3/scene_draw.hpp"
#include <algorithm>
#include <bit>
#include <set>
#include <stdexcept>
namespace wz3 {
struct FramePreloader::Impl {
  using Key=std::vector<std::uintptr_t>;
  SceneDrawCache meshes;
  std::set<Key> seen;
  std::map<std::size_t,std::pair<unsigned,unsigned>> surfaces;
  std::map<const Mesh*,std::size_t> instance_capacity;
  std::map<std::size_t,std::shared_ptr<const Texture>> supplied;
  unsigned capture_width{},capture_height{};
  std::size_t key_bytes{};
  bool remember(Key key) {
    if(seen.contains(key))return false;
    const auto bytes=sizeof(Key)+key.size()*sizeof(Key::value_type);
    if(bytes>64u*1024*1024-key_bytes)throw std::runtime_error("frame preload configuration budget exceeded");
    seen.insert(std::move(key));key_bytes+=bytes;return true;
  }
  static void shader(Key& key,const MaterialShader& s) {
    // Code/state/samplers, excluding animated colors, UV matrices and powers.
    key.insert(key.end(),s.words.begin(),s.words.begin()+24);
    for(auto texture:s.textures){key.push_back(bool(texture));key.push_back(texture.value_or(0));}
    key.push_back(bool(s.material20));
    if(s.material20) {
      const auto& m=*s.material20;key.push_back(unsigned(m.stage));
      key.insert(key.end(),m.compiled_parameters.begin(),m.compiled_parameters.end());
      for(auto texture:m.textures){key.push_back(bool(texture));key.push_back(texture.value_or(0));}
    }
  }
  bool effect(const SceneEffectDraw& effect,const FrameTarget& target) {
    Key key={3,target.width,target.height};
    if(effect.glare) {
      key.push_back(1);
      const auto passes=glare_passes(effect.glare->parameters,target.width,target.height,capture_width,capture_height);
      for(const auto& pass:passes)key.insert(key.end(),{unsigned(pass.mode),pass.source,pass.target,pass.width,pass.height});
      // A one-pass filter may allocate the other ping-pong image on its second
      // execution. Warm each layout twice, including one-frame appearances.
      return remember(std::move(key));
    }
    if(effect.color_correction){key.push_back(2);return remember(std::move(key));}
    if(effect.water){key.insert(key.end(),{3,effect.water->parameters[5]});shader(key,effect.shader);return remember(std::move(key));}
    if(effect.vertices.empty() && !effect.ribbon)return false;
    key.push_back(0);key.push_back(effect.normalized);shader(key,effect.shader);
    bool changed=remember(std::move(key));
    for(const auto& [id,texture]:effect.textures)if(supplied[id]!=texture){supplied[id]=texture;changed=true;}
    return changed;
  }
  bool frame(const DemoFrame& frame) {
    bool changed=false;
    for(const auto& [id,size]:frame.surfaces) {
      const auto dimensions=std::pair(size.width,size.height);
      if(!surfaces.contains(id) || surfaces.at(id)!=dimensions){surfaces[id]=dimensions;changed=true;}
    }
    for(const auto& command:frame.commands) {
      const auto& target=command.target;
      if(const auto* quad=std::get_if<FrameQuad>(&command.draw)) {
        if(quad->released_layer && (quad->rectangle[0]==quad->rectangle[2] || quad->rectangle[1]==quad->rectangle[3]))continue;
        Key key={1,target.width,target.height,quad->released_layer};shader(key,quad->shader);
        for(auto texture:quad->textures)key.insert(key.end(),{bool(texture),texture ? texture->surface : false,texture ? texture->id : 0});
        changed|=remember(std::move(key));
      }else if(const auto* scene=std::get_if<SceneFrame>(&command.draw)) {
        if(target.width>capture_width || target.height>capture_height)changed=true;
        capture_width=std::max(capture_width,target.width);capture_height=std::max(capture_height,target.height);
        // Posing changes vertex positions, not material groups. Animated mesh
        // bounds already use the archive's unbounded box before/after posing.
        const auto draws=scene_draw_jobs(*scene,target.width,target.height,&meshes);
        std::map<const Mesh*,Key> shadow_groups;
        for(const auto& job:draws.jobs) {
          if(job.effect){changed|=effect(scene->effects[*job.effect],target);continue;}
          const auto& instance=scene->instances[job.instance];const auto* owner=instance.mesh.get();
          const auto& batches=draws.meshes.at(owner);const auto material=batches[job.batch].material;
          const auto& m=material ? scene->materials.at(*material) : scene_default_material();const auto& pass=m.passes[job.material_pass];
          Key key={2,reinterpret_cast<std::uintptr_t>(owner),job.batch,unsigned(pass.program),unsigned(pass.usage),job.shadow_zfail,
                   target.width,target.height,m.material11_insert,m.material20_insert};
          shader(key,pass.shader);
          if(pass.program==MaterialProgram::shadow) {
            auto [found,added]=shadow_groups.try_emplace(owner);
            if(added)for(const auto& batch:batches) {
              const auto& material=batch.material ? scene->materials.at(*batch.material) : scene_default_material();
              found->second.push_back(std::any_of(material.passes.begin(),material.passes.end(),[](const auto& p){return p.program==MaterialProgram::shadow;}));
            }
            key.insert(key.end(),found->second.begin(),found->second.end());
          }
          changed|=remember(std::move(key));
          if(pass.program==MaterialProgram::instances && instance.particle_matrices) {
            auto& capacity=instance_capacity[owner];
            if(instance.particle_matrices->size()>capacity){capacity=std::bit_ceil(instance.particle_matrices->size());changed=true;}
          }
        }
      }
    }
    return changed;
  }
};
FramePreloader::FramePreloader():impl(std::make_unique<Impl>()){}
FramePreloader::~FramePreloader()=default;
bool FramePreloader::needs_draw(const DemoFrame& frame){return impl->frame(frame);}
}
