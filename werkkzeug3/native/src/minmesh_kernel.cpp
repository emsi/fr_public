// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_kernel.hpp"
#include <bit>

namespace wz3::legacy::minimal {
namespace {
GenMaterial default_material;
struct Overlay {GenMaterial* DefaultMat=&default_material;} overlay;
const auto GenOverlayManager=&overlay;
struct GenMinTempEdge {
  int FaceVert,v0,v1;
  bool operator<(const GenMinTempEdge& b)const{return v0<b.v0 || (v0==b.v0 && v1<b.v1);}
};
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
GenMinMesh::GenMinMesh() {
  // Several archived loops take &Array[0] even when Count is zero.
  Vertices.SetMax(1);Faces.SetMax(1);
  Clusters.Add()->Init(nullptr);AddCluster(nullptr,0);
}
GenMinMesh::GenMinMesh(const GenMinMesh& m):Vertices(m.Vertices),Faces(m.Faces),Clusters(m.Clusters),materials(m.materials),Animation(m.Animation),CompletelyRigid(m.CompletelyRigid){}
bool CheckMinMesh(GenMinMesh*& mesh){require(mesh!=nullptr,"missing MINMESH input");return false;}
#define sVERIFY(x) require(bool(x),"MINMESH kernel invariant: " #x)
#define sRelease(x) do {if(x)(x)->Release();(x)=nullptr;}while(false)
// Unused variables in the original routines are retained for source comparison.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "minmesh_kernel.inc"
#pragma GCC diagnostic pop
#undef sRelease
#undef sVERIFY

void GenMinMesh::validate() const {
  require(Clusters.Count>=1 && Clusters.Count<=256,"MINMESH cluster limit");
  require(Vertices.Count>=0 && std::size_t(Vertices.Count)<=kernel_limit && Faces.Count>=0 && std::size_t(Faces.Count)<=kernel_limit,"MINMESH element limit");
  for(int i=0;i<Vertices.Count;++i) {
    const auto& v=Vertices[i];
    for(const auto a:{v.Pos,v.Normal,v.Tangent})for(float c:{a.x,a.y,a.z})require(std::isfinite(c),"nonfinite MINMESH geometry");
    for(const auto& uv:v.UV)for(auto c:uv)require(std::isfinite(c),"nonfinite MINMESH UV");
    require(v.BoneCount<=4,"invalid MINMESH bone count");
    for(auto w:v.Weights)require(std::isfinite(w),"nonfinite MINMESH weight");
    if(Animation)for(unsigned j=0;j<v.BoneCount;++j)require(v.Matrix[j]<Animation->Matrices.Count,"MINMESH bone index out of bounds");
  }
  for(int i=0;i<Faces.Count;++i) {
    const auto& f=Faces[i];require(f.Count<=8 && f.Cluster<Clusters.Count,"invalid MINMESH face");
    for(int c=0;c<f.Count;++c)require(f.Vertices[c]>=0 && f.Vertices[c]<Vertices.Count,"MINMESH vertex index out of bounds");
  }
}
std::size_t GenMinMesh::bytes()const {
  return Vertices.storage.capacity()*sizeof(GenMinVert)+Faces.storage.capacity()*sizeof(GenMinFace)+Clusters.storage.capacity()*sizeof(GenMinCluster)+
    (Animation ? sizeof(GenMinMeshAnim)+Animation->Matrices.storage.capacity()*(sizeof(GenMinMatrix)+sizeof(GenSpline)+4*sizeof(sVector)+sizeof(float)) : 0);
}
Mesh export_mesh(std::shared_ptr<GenMinMesh> core) {
  core->validate();core->CalcNormals();core->validate();core->CalcAdjacency();
  std::unique_ptr<int[]> points(core->CalcMergeVerts());
  Mesh out;out.vertices.reserve(core->Vertices.Count);
  for(int i=0;i<core->Vertices.Count;++i) {
    const auto& v=core->Vertices[i];sVector color;color.InitColor(v.Color);
    out.vertices.push_back({{v.Pos.x,v.Pos.y,v.Pos.z},{v.UV[0][0],v.UV[0][1],0},v.Select,std::uint32_t(points[i]),
      {v.Tangent.x,v.Tangent.y,v.Tangent.z},{color.x,color.y,color.z,color.w}});
  }
  for(int i=0;i<core->Faces.Count;++i) {
    const auto& f=core->Faces[i];if(!f.Cluster || f.Count<3)continue;
    const auto& cl=core->Clusters[f.Cluster];
    require(cl.AnimType==0 || (cl.AnimType==2 && core->Animation && core->CompletelyRigid),"unsupported MINMESH animation type");
    MeshFace face;face.mask=f.Select;face.material=cl.Mtrl ? cl.Mtrl->binding : std::nullopt;
    face.pass=cl.RenderPass;face.casts_shadow=!(f.Flags&1);face.cluster=f.Cluster;
    if(f.Count<=4) {
      face.count=f.Count;std::copy_n(f.Vertices,f.Count,face.vertices.begin());out.faces.push_back(face);
    }else for(int c=2;c<f.Count;++c) {
      face.count=3;face.vertices={std::uint32_t(f.Vertices[0]),std::uint32_t(f.Vertices[c-1]),std::uint32_t(f.Vertices[c]),0};out.faces.push_back(face);
    }
  }
  // Keep the archive's empty-array sentinel, stitch records and deleted faces;
  // release only unused construction capacity before caching the final mesh.
  core->Vertices.compact(1);core->Faces.compact(1);core->Clusters.compact();out.faces.shrink_to_fit();
  out.min_kernel=std::move(core);validate_mesh(out);return out;
}
} // namespace wz3::legacy::minimal
