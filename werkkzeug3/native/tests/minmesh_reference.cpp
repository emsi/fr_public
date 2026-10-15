// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_reference.hpp"
#include "wz3/shadow.hpp"
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
namespace reference {
using namespace wz3::legacy;
using wz3::legacy::minimal::GenMinMesh;
using wz3::legacy::minimal::GenMinFace;
using wz3::legacy::minimal::GenMinVector;
using wz3::legacy::minimal::GenMinVert;
using wz3::legacy::minimal::GenMinCluster;
using wz3::legacy::minimal::GenMaterial;
constexpr int KMM_MAXVERT=8,sGMI_UV1=6;
struct GenMeshMtrl {GenMaterial* Material{};int Pass{};};
struct GenMesh:wz3::legacy::GenMesh {
  wz3::legacy::minimal::MinStorage<GenMeshMtrl> Mtrl;
  GenMaterial default_material;
  explicit GenMesh(const wz3::legacy::GenMesh& m):wz3::legacy::GenMesh(m) {
    Mtrl.Resize(2);Mtrl[1].Material=&default_material;
  }
  void Release(){}
};
bool CheckMesh(GenMesh* mesh){check(mesh!=nullptr,"oracle mesh input");return false;}
sU32 color(const sVector& v) {
  // sVector::GetColor, with unsigned shifts so the alpha byte cannot overflow
  // a signed integer under UBSan.
  const auto lane=[](float x){return sU32(sRange<int>(x*255,255,0));};
  return lane(v.z)|(lane(v.y)<<8)|(lane(v.x)<<16)|(lane(v.w)<<24);
}
enum {MPP_SHADOW,MPP_SPRITES,MPP_STATIC,MPP_INSTANCES,MPP_INSTANCES_SH};
struct SilEdge {int Vert[2],Face[2];};
struct EngMesh {
  struct Job {
    int Program=MPP_SHADOW,MtrlId=1,VertexCount{},IndexCount{},EdgeCount{},PlaneCount{};
    sAABox BBox;sVector* Planes{};std::vector<int> VertexBuffer,IndexBuffer,FaceMap;SilEdge* Edges{};
    void Alloc(int v,int i,int e,int f,int){VertexCount=v;IndexCount=i;EdgeCount=e;VertexBuffer.resize(v);IndexBuffer.resize(i);FaceMap.resize(f);Edges=new SilEdge[e];}
    ~Job(){delete[] Planes;delete[] Edges;}
  };
  void CalcBoundingBox(GenMinMesh*,int,sAABox&);
  void PrepareJob(Job*,GenMinMesh*,bool&,const bool*);
  void PrepareSpriteJob(Job*,GenMinMesh*){throw std::runtime_error("oracle only hosts shadows");}
};
#define sVERIFY(x) check(bool(x),"archived MINMESH oracle assertion")
#define SHADOWS 1
#define THICKLINES 0
#define sINTRO 1
#include "minmesh_oracle.inc"
#undef sINTRO
#undef THICKLINES
#undef SHADOWS
#undef sVERIFY
}
std::unique_ptr<wz3::legacy::minimal::GenMinMesh> minmesh_conversion_reference(const wz3::legacy::GenMesh& source) {
  reference::GenMesh copy(source);
  auto mesh=std::unique_ptr<wz3::legacy::minimal::GenMinMesh>(reference::Mesh_ToMin(&copy));
  // Return a stable material identity instead of the stack-owned test handle.
  wz3::legacy::minimal::GenMinMesh defaults;mesh->Clusters[1].Mtrl=defaults.Clusters[1].Mtrl;
  return mesh;
}
void check_minmesh_shadow_reference(const wz3::Mesh& mesh,std::span<const std::uint8_t> mask) {
  check(bool(mesh.min_kernel),"shadow oracle requires MINMESH");
  wz3::legacy::minimal::GenMinMesh core(*mesh.min_kernel);
  check(mask.size()==std::size_t(core.Faces.Count),"shadow oracle mask size");
  bool has[256]{};
  for(int f=0;f<core.Faces.Count;++f) {
    if(mask[f])has[core.Faces[f].Cluster]=true;
    else core.Faces[f].Flags|=1;
  }
  reference::EngMesh engine;reference::EngMesh::Job expected;bool first=true;
  engine.PrepareJob(&expected,&core,first,has);
  const auto actual=wz3::prepare_shadow_mesh(mesh,mask);
  check(actual.planes.size()==std::size_t(expected.PlaneCount),"MINMESH shadow plane count differs from archive");
  for(std::size_t i=0;i<actual.planes.size();++i)for(unsigned c=0;c<4;++c)
    check(actual.planes[i][c]==expected.Planes[i][c],"MINMESH shadow plane differs from archive");
  check(actual.edges.size()==std::size_t(expected.EdgeCount) && actual.triangles.size()==std::size_t(expected.IndexCount),"MINMESH shadow edge/cap count differs from archive");
  const auto mapped=[&](unsigned i){return unsigned(expected.VertexBuffer.at(i/2)*2)+(i&1);};
  for(std::size_t i=0;i<actual.edges.size();++i)for(unsigned c=0;c<2;++c)
    check(actual.edges[i].vertices[c]==mapped(expected.Edges[i].Vert[c]) && actual.edges[i].planes[c]==unsigned(expected.Edges[i].Face[c]),"MINMESH shadow edge differs from archive");
  for(std::size_t i=0;i<actual.triangles.size();++i)check(actual.triangles[i]==mapped(expected.IndexBuffer[i]),"MINMESH shadow cap differs from archive");
  for(std::size_t i=0;i<actual.triangle_planes.size();++i)check(actual.triangle_planes[i]==unsigned(expected.FaceMap[i]),"MINMESH shadow face map differs from archive");
}
