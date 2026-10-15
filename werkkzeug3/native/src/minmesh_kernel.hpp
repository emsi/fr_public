// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Portable storage for genminmesh.cpp. Keep MINMESH's polygon, stitch and
// normal semantics separate from the older indexed GenMesh implementation.
#pragma once
#include "mesh_kernel.hpp"
#include <map>

namespace wz3::legacy {struct BlobSpline;struct BlobPipe;}
namespace wz3::legacy::minimal {
using sU16=std::uint16_t;using sS8=std::int8_t;using sF64=double;
constexpr int KMM_MAXBONE=4,KMM_MAXUV=2,KMM_MAXVERT=8,KC_MATERIAL=1;
constexpr int MMS_ADD=0,MMS_SUB=1,MMS_SET=2,MMS_SETNOT=3;
constexpr int MMS_VERTEX=0,MMS_FULLFACE=4,MMS_PARTFACE=8;
constexpr int MMU_ALL=0,MMU_SELECTED=1,MMU_UNSELECTED=2;
constexpr float sPIF=sPI;
inline double sFSqrt(double x){return std::sqrt(x);}
inline double sFInvSqrt(double x){return 1.0/std::sqrt(x);}
inline double sFSin(double x){return std::sin(x);}
inline double sFCos(double x){return std::cos(x);}
inline double sFATan2(double x,double y){return std::atan2(x,y);}
inline int sCmpMem(const void* a,const void* b,std::size_t n){return std::memcmp(a,b,n);}
inline void sCopyMem(void* a,const void* b,std::size_t n){if(n)std::memcpy(a,b,n);}
template<class T> void sSwap(T& a,T& b){std::swap(a,b);}

struct GenMinVector {
  float x{},y{},z{};
  void Init(){x=y=z=0;}
  void Init(float a,float b,float c){x=a;y=b;z=c;}
  void Init(const sVector& v){Init(v.x,v.y,v.z);}
  void Out(sVector& v,float w) const {v.Init(x,y,z,w);}
  void Add(const GenMinVector& v){x+=v.x;y+=v.y;z+=v.z;}
  void Sub(const GenMinVector& v){x-=v.x;y-=v.y;z-=v.z;}
  void Add(const GenMinVector& a,const GenMinVector& b){x=a.x+b.x;y=a.y+b.y;z=a.z+b.z;}
  void Sub(const GenMinVector& a,const GenMinVector& b){x=a.x-b.x;y=a.y-b.y;z=a.z-b.z;}
  void Lin3(const GenMinVector& a,const GenMinVector& b,float t){x=a.x+t*(b.x-a.x);y=a.y+t*(b.y-a.y);z=a.z+t*(b.z-a.z);}
  void AddScale(const GenMinVector& a,float s){x+=a.x*s;y+=a.y*s;z+=a.z*s;}
  void Scale(float s){x*=s;y*=s;z*=s;}
  float Dot(const GenMinVector& a)const{return x*a.x+y*a.y+z*a.z;}
  float Len()const{return sFSqrt(x*x+y*y+z*z);}
  void UnitSafe();void Cross(const GenMinVector&,const GenMinVector&);
};
struct GenMinVert {
  sU8 Select{},BoneCount{},TempByte{},MergeTag{};
  sU32 Color{};
  GenMinVector Pos,Normal,Tangent;
  float UV[2][2]{},Weights[4]{};
  sU16 Matrix[4]{};
  bool IsSame(const GenMinVert&,bool);
};
struct GenMinFace {
  sU8 Select{},Count{},Cluster{},Temp{};
  GenMinVector Normal;
  sU32 Flags{};
  int Vertices[8]{},Adjacent[8]{};
};
// Material pointers are identity handles only. The arena outlives the mesh;
// MaterialRuntime owns the actual shaders and animated state.
struct GenMaterial {
  std::optional<std::size_t> binding;
  int ClassId=KC_MATERIAL;
  void AddRef(){} void Release(){}
};
using MaterialArena=std::map<std::size_t,GenMaterial>;
struct GenMinCluster {
  GenMaterial* Mtrl{};
  int RenderPass{},Id{},AnimType{},AnimMatrix=-1,Temp{};
  void Init(GenMaterial* m,int p=0,int id=0,int anim=0,int matrix=-1){Mtrl=m;RenderPass=p;Id=id;AnimType=anim;AnimMatrix=matrix;}
  bool Equals(const GenMinCluster& b)const{return Mtrl==b.Mtrl&&RenderPass==b.RenderPass&&Id==b.Id&&AnimType==b.AnimType&&AnimMatrix==b.AnimMatrix;}
};
struct GenSpline {
  virtual ~GenSpline()=default;
  virtual void Eval(float time,float phase,sMatrix&,float& zoom)=0;
  virtual std::size_t bytes()const{return sizeof(GenSpline)+4*sizeof(sVector)+sizeof(float);}
};
struct BoneWeights {float Weight[4]{};sU16 Matrix[4]{};};
BoneWeights bone_weights(const GenMinVert&);
struct GenMinMatrix {
  sMatrix BasePose,Temp,NoAnimation;
  int Parent=-1;
  float Offset=0,Factor=1,Spread=0;
  std::shared_ptr<GenSpline> Spline;
  GenMinMatrix(){BasePose.Init();Temp.Init();NoAnimation.Init();}
};
template<class T> struct MinStorage:Storage<T> {
  void SetMax(int n){this->AtLeast(n);}
  void Resize(int n){SetMax(n);this->Count=n;}
};
struct GenMinMeshAnim {
  MinStorage<GenMinMatrix> Matrices;
  explicit GenMinMeshAnim(int n){Matrices.Resize(n);Matrices.compact();}
  void EvalAnimation(float time,float metamorph,sMatrix*);
};
class GenMinMesh {
  static sU32 HashVector(const GenMinVector&);
  bool NormalsOK{};
public:
  MinStorage<GenMinVert> Vertices;
  MinStorage<GenMinFace> Faces;
  MinStorage<GenMinCluster> Clusters;
  std::shared_ptr<MaterialArena> materials;
  std::shared_ptr<GenMinMeshAnim> Animation;
  bool CompletelyRigid=false;
  GenMinMesh();
  GenMinMesh(const GenMinMesh&);
  GenMinMesh& operator=(const GenMinMesh&)=delete;
  void ChangeGeo(){NormalsOK=false;} void ChangeTopo(){NormalsOK=false;}
  GenMinVert* MakeGrid(int,int,int);
  int AddCluster(GenMaterial*,int,int=0,int=0,int=-1);
  void Transform(int,const sMatrix&,int=0,int=0,int=0);
  void CalcNormals();void Add(const GenMinMesh*);void MergeClusters();void Invert();
  void CalcBBox(sAABox&)const;void SelectAll(int,int);
  void CreateAnimation(int n){Animation=std::make_shared<GenMinMeshAnim>(n);}
  int* CalcMergeVerts()const;bool CalcAdjacencyCore(const int*);bool CalcAdjacency();
  void validate()const;std::size_t bytes()const;
};
bool CheckMinMesh(GenMinMesh*&);
GenMinMesh* MinMesh_Grid(int,int,int);
GenMinMesh* MinMesh_Cube(int,int,int,int,sFSRT);
GenMinMesh* MinMesh_Sphere(int,int);
GenMinMesh* MinMesh_MatLink(GenMinMesh*,GenMaterial*,int,int);
GenMinMesh* MinMesh_MatLinkId(GenMinMesh*,GenMaterial*,int,int);
GenMinMesh* MinMesh_SelectAll(GenMinMesh*,sU32);
GenMinMesh* MinMesh_SelectCube(GenMinMesh*,int,sF323,sF323);
GenMinMesh* MinMesh_SelectLogic(GenMinMesh*,int);
GenMinMesh* MinMesh_DeleteFaces(GenMinMesh*);
GenMinMesh* MinMesh_Invert(GenMinMesh*);
GenMinMesh* MinMesh_TransformEx(GenMinMesh*,int,sFSRT);
GenMinMesh* MinMesh_ExtrudeNormal(GenMinMesh*,int,float);
GenMinMesh* MinMesh_Bend2(GenMinMesh*,sF323,sF323,float,float);
GenMinMesh* MinMesh_Triangulate(GenMinMesh*);
GenMinMesh* MinMesh_Center(GenMinMesh*,int);
GenMinMesh* MinMesh_Perlin(GenMinMesh*,int,sFSRT,sF323);
GenMinMesh* explode(GenMinMesh&,const Texture*,std::span<const std::uint32_t>);
GenMinMesh* bone_chain(GenMinMesh&,std::span<const std::uint32_t>);
GenMinMesh* bone_train(GenMinMesh&,std::shared_ptr<GenSpline>,float delta,unsigned mode,float offset);
std::shared_ptr<GenSpline> bone_spline(const Project&,const Graph&,std::size_t);
GenMinMesh* pipe_mesh(BlobSpline&,BlobPipe&,const GenMinMesh&,const GenMinMesh*,const GenMinMesh*,std::span<const std::uint32_t>);
Mesh export_mesh(std::shared_ptr<GenMinMesh>);
std::shared_ptr<const Mesh> evaluate_kernel(const Project&,const Graph&,std::size_t,const TextureOptions& = {});
} // namespace wz3::legacy::minimal
