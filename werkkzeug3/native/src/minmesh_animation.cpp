// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_kernel.hpp"
#include "particle_random.hpp"
#include "wz3/texture.hpp"
#include <bit>

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
bool mesh_has_animation(const Mesh& mesh){return mesh.min_kernel && bool(mesh.min_kernel->Animation);}
std::shared_ptr<const Mesh> pose_mesh(const std::shared_ptr<const Mesh>& mesh,float time) {
  if(!mesh || !mesh_has_animation(*mesh))return mesh;
  if(!std::isfinite(time))throw std::runtime_error("nonfinite mesh animation time");
  const auto& base=mesh->animation_source ? mesh->animation_source : mesh;
  const auto& source=*base->min_kernel;
  auto animation=*source.Animation;std::vector<legacy::sMatrix> matrices(animation.Matrices.Count);
  animation.EvalAnimation(time,0,matrices.data());
  const auto need_bones=std::any_of(source.Clusters.Array+1,source.Clusters.Array+source.Clusters.Count,[](const auto& c){return c.AnimType==2;});
  if(need_bones && !source.CompletelyRigid)throw std::runtime_error("weighted MINMESH animation is not ported");
  auto core=std::make_shared<legacy::minimal::GenMinMesh>(source);
  auto posed=std::make_shared<Mesh>(*base);posed->animation_source=base;posed->min_kernel=core;
  if(need_bones)for(int i=0;i<core->Vertices.Count;++i) {
    auto& v=core->Vertices[i];if(v.Matrix[0]>=matrices.size())throw std::runtime_error("rigid mesh bone index out of bounds");
    const auto& matrix=matrices[v.Matrix[0]];legacy::sVector value;
    v.Pos.Out(value,1);value.Rotate34(matrix);v.Pos.Init(value);
    v.Normal.Out(value,0);value.Rotate3(matrix);v.Normal.Init(value);
    v.Tangent.Out(value,0);value.Rotate3(matrix);v.Tangent.Init(value);
    posed->vertices[i].position={v.Pos.x,v.Pos.y,v.Pos.z};posed->vertices[i].tangent={v.Tangent.x,v.Tangent.y,v.Tangent.z};
  }
  // Keep bind-pose topology, UVs, crease normals and weld indices. Rebuilding
  // normals here would merge fragments and alter the original skinned shading.
  core->validate();validate_mesh(*posed);return posed;
}
} // namespace wz3
