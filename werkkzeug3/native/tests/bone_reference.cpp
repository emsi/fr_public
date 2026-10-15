// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "bone_reference.hpp"
#include <cstdlib>
#include <cstdint>
#include <cfenv>
namespace source {
using namespace wz3::legacy;
using namespace wz3::legacy::minimal;
struct alignas(16) sVertexTSpace3Big {float x,y,z,nx,ny,nz,sx,sy,sz;std::uint32_t c;float u,v;};
static_assert(sizeof(sVertexTSpace3Big)==48);
static_assert(sizeof(sMatrix)==64);
struct EngMesh {
  using BoneInfo=BoneWeights;
  sVertexTSpace3Big* Vert{};int VertCount{};BoneInfo* BoneInfos{};sU16* MatrixInds{};bool CompletelyRigid{};
  ~EngMesh(){std::free(Vert);delete[] BoneInfos;delete[] MatrixInds;}
  void FillVertexBuffer(GenMinMesh*);
};
void _aligned_free(void* p){std::free(p);}
void* _aligned_malloc(std::size_t bytes,std::size_t alignment){auto* p=std::aligned_alloc(alignment,bytes);if(bytes && !p)throw std::bad_alloc();return p;}
#include "bone_oracle.inc"
#if WZ3_BONE_SSE_ORACLE
extern "C" void fr_bone_sse(sVertexTSpace3Big*,const sVertexTSpace3Big*,EngMesh::BoneInfo*,sMatrix*,int);
#endif
}
std::shared_ptr<const wz3::Mesh> bone_reference_pose(const std::shared_ptr<const wz3::Mesh>& mesh,float time) {
  struct Nearest {int saved=std::fegetround();Nearest(){std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetround(saved);}} rounding;
  namespace mm=wz3::legacy::minimal;
  auto core=std::make_shared<mm::GenMinMesh>(*mesh->min_kernel);source::EngMesh engine;engine.FillVertexBuffer(core.get());
  auto animation=*core->Animation;std::vector<wz3::legacy::sMatrix> matrices(animation.Matrices.Count);animation.EvalAnimation(time,0,matrices.data());
  std::vector<source::sVertexTSpace3Big> result(engine.VertCount),fpu(engine.VertCount);
  source::FullBoneInner(fpu.data(),engine.Vert,engine.BoneInfos,matrices.data(),engine.VertCount);
#if WZ3_BONE_SSE_ORACLE
  if(std::uintptr_t(matrices.data())%16)throw std::runtime_error("unaligned reference matrices");
  source::fr_bone_sse(result.data(),engine.Vert,engine.BoneInfos,matrices.data(),engine.VertCount);
#else
  result=fpu;
#endif
  auto out=std::make_shared<wz3::Mesh>(*mesh);out->min_kernel=core;out->animation_source=mesh;core->Animation.reset();
  for(unsigned i=0;i<result.size();++i) {
    const auto& a=result[i];const auto& b=fpu[i];
    const std::array<float,9> sse{a.x,a.y,a.z,a.nx,a.ny,a.nz,a.sx,a.sy,a.sz},scalar{b.x,b.y,b.z,b.nx,b.ny,b.nz,b.sx,b.sy,b.sz};
    for(unsigned c=0;c<9;++c)if(std::abs(sse[c]-scalar[c])>2e-5f*std::max(1.f,std::abs(sse[c])))throw std::runtime_error("translated SSE differs from archived FPU skinning");
    auto& v=core->Vertices[i];v.Pos.Init(a.x,a.y,a.z);v.Normal.Init(a.nx,a.ny,a.nz);v.Tangent.Init(a.sx,a.sy,a.sz);
    out->vertices[i].position={a.x,a.y,a.z};out->vertices[i].tangent={a.sx,a.sy,a.sz};
  }
  return out;
}
