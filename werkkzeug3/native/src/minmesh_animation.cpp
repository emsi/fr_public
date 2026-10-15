// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_kernel.hpp"
#include "particle_random.hpp"
#include "wz3/texture.hpp"
#include <bit>
#include <cfenv>

namespace wz3::legacy::minimal {
namespace {
using sU64=std::uint64_t;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct GenBitmap {const Texture* Data;unsigned XSize,YSize;};
struct BilinearContext {const Texture* texture;unsigned border;};
void BilinearSetup(BilinearContext* c,const Texture* texture,unsigned,unsigned,unsigned border){*c={texture,border};}
void BilinearFilter(const BilinearContext* c,sU64* out,int u,int v) {
  const auto pixel=sample_bitmap(*c->texture,u,v,c->border);*out=0;
  for(unsigned i=0;i<4;++i)*out|=std::uint64_t(pixel[i])<<(i*16);
}
int fixed_coordinate(float v) {
  require(std::isfinite(v) && double(v)>=-2147483648.0 && double(v)<2147483648.0,"Explode bitmap coordinate overflow");return int(v);
}
thread_local ParticleRandom random;
void sSetRndSeed(unsigned seed){random.seed(seed);}
float sFGetRnd(){return random.real();}
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "explode_kernel.inc"
#pragma GCC diagnostic pop

GenMinMesh* explode(GenMinMesh& input,const Texture* bitmap,std::span<const std::uint32_t> p) {
  require(p.size()==21 && p[15]<=7,"invalid Explode parameters");
  const auto real=[&](unsigned i){const auto v=as_float(p[i]);require(std::isfinite(v),"nonfinite Explode parameter");return v;};
  const sVector center{real(0),real(1),real(2),0},towards{real(16),real(17),real(18),0};
  if((p[15]&1) && !bitmap)require(towards.x!=center.x || towards.y!=center.y || towards.z!=center.z,"Explode direction is zero");
  const GenBitmap texture{bitmap,bitmap ? bitmap->width : 0,bitmap ? bitmap->height : 0};
  auto mesh=std::unique_ptr<GenMinMesh>(MinMesh_Explode(&input,bitmap ? &texture : nullptr,center,real(3),real(4),real(5),real(6),real(7),
    {real(8),real(9),real(10),0},real(11),real(12),real(13),real(14),p[15],towards,real(19),real(20)));
  if(mesh->Animation)for(int i=0;i<mesh->Animation->Matrices.Count;++i)require(std::isfinite(mesh->Animation->Matrices[i].Offset),"nonfinite Explode delay");
  return mesh.release();
}
} // namespace wz3::legacy::minimal

namespace wz3 {
namespace {
legacy::sMatrix weighted_matrix(const legacy::minimal::BoneWeights& weights,std::span<const legacy::sMatrix> matrices) {
  legacy::sMatrix result;
  // FullBoneInnerSSE starts with the first product, then accumulates the other
  // influences in the upload's sorted/pruned order. Preserve negative weights.
  const auto add=[&](unsigned bone,bool first) {
    const auto index=weights.Matrix[bone];const auto weight=weights.Weight[bone];
    if(index>=matrices.size() || !std::isfinite(weight))throw std::runtime_error("invalid weighted mesh bone");
    const auto& m=matrices[index];
    const legacy::sVector* in[]={&m.i,&m.j,&m.k,&m.l};legacy::sVector* out[]={&result.i,&result.j,&result.k,&result.l};
    for(unsigned c=0;c<4;++c)for(unsigned r=0;r<3;++r){const auto value=(*in[c])[r]*weight;if(first)(*out[c])[r]=value;else (*out[c])[r]+=value;}
  };
  add(0,true);
  for(unsigned b=1;b<4 && std::bit_cast<std::uint32_t>(weights.Weight[b])!=0;++b)add(b,false);
  return result;
}
void weighted_vertex(legacy::minimal::GenMinVert& v,const legacy::sMatrix& m) {
  const auto p=v.Pos,n=v.Normal,t=v.Tangent;
  // The assembly groups position as (translation + X) + (Y + Z).
  v.Pos.Init((m.l.x+p.x*m.i.x)+(p.y*m.j.x+p.z*m.k.x),
             (m.l.y+p.x*m.i.y)+(p.y*m.j.y+p.z*m.k.y),
             (m.l.z+p.x*m.i.z)+(p.y*m.j.z+p.z*m.k.z));
  v.Normal.Init((n.y*m.j.x+n.x*m.i.x)+n.z*m.k.x,(n.y*m.j.y+n.x*m.i.y)+n.z*m.k.y,(n.y*m.j.z+n.x*m.i.z)+n.z*m.k.z);
  v.Tangent.Init((t.x*m.i.x+t.y*m.j.x)+t.z*m.k.x,(t.x*m.i.y+t.y*m.j.y)+t.z*m.k.y,(t.x*m.i.z+t.y*m.j.z)+t.z*m.k.z);
}
}
bool mesh_has_animation(const Mesh& mesh){return mesh.min_kernel && bool(mesh.min_kernel->Animation);}
struct MeshPoseCache::Impl {
  struct Entry {
    std::shared_ptr<const Mesh> base;
    bool need_bones{};
    std::vector<legacy::minimal::BoneWeights> groups;
    std::vector<std::uint32_t> vertex_groups;
    std::vector<std::shared_ptr<Mesh>> pool;
  };
  std::map<const Mesh*,Entry> entries;std::size_t bytes{},pool_bytes{};
  Entry& prepare(const std::shared_ptr<const Mesh>& base) {
    if(const auto found=entries.find(base.get());found!=entries.end())return found->second;
    validate_mesh(*base);base->min_kernel->validate();const auto& source=*base->min_kernel;
    if(base->vertices.size()!=std::size_t(source.Vertices.Count))throw std::runtime_error("posed mesh vertex count mismatch");
    Entry entry;entry.base=base;
    entry.need_bones=std::any_of(source.Clusters.Array+1,source.Clusters.Array+source.Clusters.Count,[](const auto& c){return c.AnimType==2;});
    if(entry.need_bones && !source.CompletelyRigid) {
      std::map<std::array<std::uint32_t,8>,std::uint32_t> groups;
      entry.vertex_groups.reserve(source.Vertices.Count);
      for(int i=0;i<source.Vertices.Count;++i) {
        const auto& vertex=source.Vertices[i];
        const auto weights=legacy::minimal::bone_weights(vertex);std::array<std::uint32_t,8> key;
        for(unsigned i=0;i<4;++i){key[i]=std::bit_cast<std::uint32_t>(weights.Weight[i]);key[4+i]=weights.Matrix[i];}
        const auto [found,added]=groups.try_emplace(key,entry.groups.size());
        if(added)entry.groups.push_back(weights);
        entry.vertex_groups.push_back(found->second);
      }
    }
    const auto size=entry.groups.capacity()*sizeof(legacy::minimal::BoneWeights)+entry.vertex_groups.capacity()*sizeof(std::uint32_t);
    if(size>128u*1024*1024-bytes)throw std::runtime_error("mesh pose cache budget exceeded");
    bytes+=size;return entries.emplace(base.get(),std::move(entry)).first->second;
  }
  std::shared_ptr<Mesh> acquire(Entry& entry) {
    // A returned pose is immutable for as long as any caller/GPU cache retains
    // either it or its MINMESH core. Reuse only exclusively owned storage.
    for(const auto& pose:entry.pool)if(pose.use_count()==1 && pose->min_kernel.use_count()==1)return pose;
    auto pose=std::make_shared<Mesh>(*entry.base);pose->animation_source=entry.base;
    pose->min_kernel=std::make_shared<legacy::minimal::GenMinMesh>(*entry.base->min_kernel);
    const auto size=mesh_storage_bytes(*pose);
    if(entry.pool.size()<3 && size<=128u*1024*1024-pool_bytes){entry.pool.push_back(pose);pool_bytes+=size;}
    return pose;
  }
};
MeshPoseCache::MeshPoseCache():impl(std::make_unique<Impl>()){}
MeshPoseCache::~MeshPoseCache()=default;
std::shared_ptr<const Mesh> MeshPoseCache::pose(const std::shared_ptr<const Mesh>& mesh,float time) {
  if(!mesh || !mesh_has_animation(*mesh))return mesh;
  if(!std::isfinite(time))throw std::runtime_error("nonfinite mesh animation time");
  struct Nearest {int saved=std::fegetround();Nearest(){if(saved==-1 || std::fesetround(FE_TONEAREST))throw std::runtime_error("cannot set bone pose rounding mode");}~Nearest(){std::fesetround(saved);}} rounding;
  const auto& base=mesh->animation_source ? mesh->animation_source : mesh;
  auto& prepared=impl->prepare(base);const auto& source=*base->min_kernel;
  auto animation=*source.Animation;std::vector<legacy::sMatrix> matrices(animation.Matrices.Count);
  animation.EvalAnimation(time,0,matrices.data());
  std::vector<legacy::sMatrix> blends;blends.reserve(prepared.groups.size());
  for(const auto& weights:prepared.groups)blends.push_back(weighted_matrix(weights,matrices));
  auto posed=impl->acquire(prepared);
  auto core=std::const_pointer_cast<legacy::minimal::GenMinMesh>(posed->min_kernel);
  if(prepared.need_bones)for(int i=0;i<core->Vertices.Count;++i) {
    auto& v=core->Vertices[i];const auto& bind=source.Vertices[i];
    v.Pos=bind.Pos;v.Normal=bind.Normal;v.Tangent=bind.Tangent;
    if(source.CompletelyRigid) {
      if(v.Matrix[0]>=matrices.size())throw std::runtime_error("rigid mesh bone index out of bounds");
      const auto& matrix=matrices[v.Matrix[0]];legacy::sVector value;
      v.Pos.Out(value,1);value.Rotate34(matrix);v.Pos.Init(value);
      v.Normal.Out(value,0);value.Rotate3(matrix);v.Normal.Init(value);
      v.Tangent.Out(value,0);value.Rotate3(matrix);v.Tangent.Init(value);
    }else weighted_vertex(v,blends[prepared.vertex_groups[i]]);
    posed->vertices[i].position={v.Pos.x,v.Pos.y,v.Pos.z};posed->vertices[i].tangent={v.Tangent.x,v.Tangent.y,v.Tangent.z};
  }
  // Keep bind-pose topology, UVs, crease normals and weld indices. Rebuilding
  // normals here would merge fragments and alter the original skinned shading.
  // Only positions, normals and tangents changed. Topology, UVs, colors and
  // bone bindings were validated once on the retained immutable source.
  for(int i=0;i<core->Vertices.Count;++i)for(const auto value:{core->Vertices[i].Pos,core->Vertices[i].Normal,core->Vertices[i].Tangent})
    for(float c:{value.x,value.y,value.z})if(!std::isfinite(c))throw std::runtime_error("nonfinite posed mesh geometry");
  return posed;
}
std::shared_ptr<const Mesh> pose_mesh(const std::shared_ptr<const Mesh>& mesh,float time) {
  MeshPoseCache cache;return cache.pose(mesh,time);
}
} // namespace wz3
