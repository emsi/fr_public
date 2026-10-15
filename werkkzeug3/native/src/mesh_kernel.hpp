// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Portable host for the archived indexed CPU mesh routines. No graphics API,
// Windows ABI, KObject lifetime or engine material objects are required here.
#pragma once
#include "wz3/mesh.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace wz3 {struct Texture;}
namespace wz3::legacy {
using sInt=int; using sU32=std::uint32_t; using sF32=float; using sU8=std::uint8_t; using sBool=bool;
constexpr bool sTRUE=true,sFALSE=false;
constexpr float sPI=3.14159265358979323846f,sPI2F=2*sPI;
constexpr double sPI2=6.28318530717958647692;
constexpr int MSM_ADD=0,MSM_SUB=1,MSM_SET=2,MSM_SETNOT=3,MAS_EDGE=1,MAS_FACE=2,MAS_VERT=4;
constexpr int sGMI_POS=0,sGMI_NORMAL=1,sGMI_TANGENT=2,sGMI_COLOR0=3,sGMI_UV0=5;
constexpr int sGMF_POS=1,sGMF_NORMAL=2,sGMF_TANGENT=4,sGMF_NORMALS=6,sGMF_UV0=32,sGMF_DEFAULT=47,sGMF_ALL=511;
constexpr std::size_t kernel_limit=1<<20;
struct KOp {};
template<class T> T sMin(T a,T b) {return std::min(a,b);}
template<class T> T sMax(T a,T b) {return std::max(a,b);}
template<class T> T sRange(T v,T hi,T lo) {return std::clamp(v,lo,hi);}
inline float sFAbs(float v) {return std::abs(v);}
inline float sFSin(float v) {return std::sin(double(v));}
inline float sFCos(float v) {return std::cos(double(v));}
inline float sFPow(float v,float p) {return std::pow(double(v),double(p));}
inline float sFInvSqrt(float v) {return 1/std::sqrt(v);}
inline void sSetMem(void* p,int v,std::size_t n) {std::memset(p,v,n);}
inline void sSetMem4(void* p,sU32 v,std::size_t n) {
  for (std::size_t i=0;i<n;++i) std::memcpy(static_cast<char*>(p)+4*i,&v,4);
}
inline void sCopyMem4(void* d,const void* s,std::size_t n) {std::memcpy(d,s,n*4);}

struct sMatrix;
struct sVector {
  float x{},y{},z{},w{};
  float& operator[](unsigned i) {if(i==0)return x;if(i==1)return y;if(i==2)return z;if(i==3)return w;throw std::runtime_error("vector index");}
  const float& operator[](unsigned i) const {return const_cast<sVector*>(this)->operator[](i);}
  void Init(float a=0,float b=0,float c=0,float d=1) {x=a;y=b;z=c;w=d;}
  void Init3(float a,float b,float c) {x=a;y=b;z=c;}
  void Init4(float a,float b,float c,float d) {Init(a,b,c,d);}
  void InitColor(sU32 c) {Init(((c>>16)&255)/255.0f,((c>>8)&255)/255.0f,(c&255)/255.0f,(c>>24)/255.0f);}
  void Lin3(const sVector& a,const sVector& b,float t) {x=a.x+(b.x-a.x)*t;y=a.y+(b.y-a.y)*t;z=a.z+(b.z-a.z)*t;}
  void Lin4(const sVector& a,const sVector& b,float t) {Lin3(a,b,t);w=a.w+(b.w-a.w)*t;}
  void Add3(const sVector& a,const sVector& b) {x=a.x+b.x;y=a.y+b.y;z=a.z+b.z;}
  void Add3(const sVector& a) {x+=a.x;y+=a.y;z+=a.z;}
  void Add4(const sVector& a) {Add3(a);w+=a.w;}
  void Add4(const sVector& a,const sVector& b) {x=a.x+b.x;y=a.y+b.y;z=a.z+b.z;w=a.w+b.w;}
  void Sub3(const sVector& a,const sVector& b) {x=a.x-b.x;y=a.y-b.y;z=a.z-b.z;}
  void Sub3(const sVector& a) {x-=a.x;y-=a.y;z-=a.z;}
  void Scale3(float s) {x*=s;y*=s;z*=s;}
  void Scale4(float s) {Scale3(s);w*=s;}
  void Scale3(const sVector& a,float s) {x=a.x*s;y=a.y*s;z=a.z*s;}
  void Scale4(const sVector& a,float s) {Scale3(a,s);w=a.w*s;}
  void AddScale3(const sVector& a,float s) {x+=a.x*s;y+=a.y*s;z+=a.z*s;}
  void AddScale4(const sVector& a,float s) {AddScale3(a,s);w+=a.w*s;}
  void AddMul3(const sVector& a,const sVector& b) {x+=a.x*b.x;y+=a.y*b.y;z+=a.z*b.z;}
  void Mul3(const sVector& a) {x*=a.x;y*=a.y;z*=a.z;}
  void Mul4(const sVector& a) {Mul3(a);w*=a.w;}
  float Dot3(const sVector& a) const {return x*a.x+y*a.y+z*a.z;}
  void Cross3(const sVector& a,const sVector& b) {const auto xx=a.y*b.z-a.z*b.y,yy=a.z*b.x-a.x*b.z,zz=a.x*b.y-a.y*b.x;x=xx;y=yy;z=zz;}
  void Cross4(const sVector& a,const sVector& b) {Cross3(a,b);w=0;}
  float Abs3() const {return std::sqrt(Dot3(*this));}
  void Unit3() {Scale3(sFInvSqrt(Dot3(*this)));}
  void UnitSafe3() {if(Dot3(*this)>1e-20f)Unit3();else Init3(1,0,0);}
  float UnitAbs3() {const float e=std::sqrt(double(Dot3(*this)));if(e>1e-10f)Scale3(1/e);else Init3(1,0,0);return e;}
  void Rotate3(const sMatrix& m,const sVector& v);
  void Rotate3(const sMatrix& m) {Rotate3(m,*this);}
  void Rotate34(const sMatrix& m,const sVector& v);
  void Rotate34(const sMatrix& m) {Rotate34(m,*this); }
};
struct sMatrix {
  sVector i,j,k,l;
  void Init() {i.Init(1,0,0,0);j.Init(0,1,0,0);k.Init(0,0,1,0);l.Init(0,0,0,1);}
  void InitEuler(float,float,float);
  void InitEulerPI2(const float*);
  void InitEulerPI2(const sVector& v) {const float a[]={v.x,v.y,v.z};InitEulerPI2(a);}
  void InitSRT(const float*);
  void InitDir(const sVector&);
  void InitRot(const sVector&,float);
  void Mul3(const sMatrix&,const sMatrix&);
  void MulA(const sMatrix&,const sMatrix&);
  void MulA(const sMatrix& b) {MulA(*this,b);}
  void TransR();
};
using sF323=sVector;
using sF322=sVector;
struct sInt3 {int x,y,z;void Init(int a,int b,int c) {x=a;y=b;z=c;}};
struct sFSRT {sVector s,r,t;float v[9];};
struct sAABox {sVector Min,Max;};

template<class T> struct Storage {
  std::vector<T> storage;
  int Count{};
  T* Array{};
  Storage()=default;
  Storage(const Storage& a):storage(a.storage),Count(a.Count),Array(storage.data()) {}
  Storage& operator=(const Storage& a) {storage=a.storage;Count=a.Count;Array=storage.data();return *this;}
  void AtLeast(int n) {
    if(n<0 || std::size_t(n)>kernel_limit) throw std::runtime_error("indexed mesh element limit");
    if(std::size_t(n)>storage.size()) storage.resize(std::max(std::size_t(n),std::min(kernel_limit,storage.size()*2+16)));
    Array=storage.data();
  }
  void compact(int minimum=0) {
    if(Count<0 || std::size_t(Count)>storage.size())throw std::runtime_error("invalid indexed mesh storage count");
    storage.resize(std::max(Count,minimum));storage.shrink_to_fit();Array=storage.data();
  }
  T* Add() {AtLeast(Count+1);return &Array[Count++];}
  T& operator[](int i) {if(i<0 || std::size_t(i)>=storage.size())throw std::runtime_error("indexed mesh array bounds");return Array[i];}
  const T& operator[](int i) const {return const_cast<Storage*>(this)->operator[](i);}
};
struct GenMeshElem {
  sU8 Mask{},Id{},Select{},Used{};
  void InitElem(); void SelElem(sU32,sBool,sInt); void SelLogic(sU32,sU32,sU32,sInt);
};
struct GenMeshVert:GenMeshElem {
  int Next{},First{},Temp{},Temp2{},ReIndex{};
  sU8 Weight[4]{},Matrix[4]{};
  void Init(); void Sel(sU32 m,sBool s,sInt mode) {SelElem(m>>16,s,mode);}
};
struct GenMeshEdge:GenMeshElem {
  int Next[2]{},Prev[2]{},Face[2]{},Vert[2]{},Temp[2]{},Crease{};
  void Init(); void Sel(sU32 m,sBool s,sInt mode) {SelElem(m,s,mode);}
};
struct GenMeshFace:GenMeshElem {
  int Material{},Edge{},Temp{},Temp2{},Temp3{};
  std::optional<std::size_t> binding;
  std::int32_t pass{};
  void Init(); void Sel(sU32 m,sBool s,sInt mode) {SelElem(m>>8,s,mode);}
};
struct Collision {int Vert[8];};

class GenMesh {
public:
  Storage<GenMeshVert> Vert;
  Storage<GenMeshEdge> Edge;
  Storage<GenMeshFace> Face;
  Storage<Collision> Coll;
  Storage<int> Parts;
  Storage<int> Lgts;
  std::vector<sVector> buffer;
  sVector* VertBuf{};
  bool GotNormals{};
  GenMesh()=default;
  GenMesh(const GenMesh&);
  GenMesh& operator=(const GenMesh&)=delete;
  void Init(sU32,int); void Realloc(int);void compact_storage();
  int VertSize() const {return 5;} int VertMask() const {return sGMF_DEFAULT;}
  int VertMap(int i) const {return i<4 ? i : i==5 ? 4 : -1;}
  sVector& VertPos(int v) {return buffer.at(v*5);}
  sVector& VertNorm(int v) {return buffer.at(v*5+1);}
  GenMeshEdge* GetEdge(sU32 i) {return &Edge[i>>1];}
  GenMeshFace* GetFace(sU32); GenMeshFace* GetFaceI(sU32); GenMeshVert* GetVert(sU32);
  int GetFaceId(sU32);int GetVertId(sU32);int NextFaceEdge(sU32);int PrevFaceEdge(sU32);
  int NextVertEdge(sU32);int PrevVertEdge(sU32);int SkipFaceEdge(sU32,int);
  int AddVert();int AddNewVert();int AddCopiedVert(int);
  void SplitFace(sU32,sU32,sU32=0,int=0);
  void SplitBridge(sU32,sU32,int&,int&,sU32=0,int=0,int* =nullptr,int* =nullptr);
  void MakeFace(int,int,...);
  void ReIndex();bool IsBorderEdge(sU32,int);
  void Mask2Sel(sU32);void All2Sel(bool=true,int=7);void Sel2Mask(sU32,int=0);void All2Mask(sU32,int=0);
  void Face2Vert();void Edge2Vert(int);void Vert2FaceEdge();
  void CalcNormal(sVector&,int,int,int);void CalcFaceNormal(sVector&,int);void CalcFaceNormalAccurate(sVector&,int);
  void CalcFaceCenter(sVector&,int);void CalcBBox(sAABox&);void CopyVert(int,int);
  void TransVert(sMatrix&,int=0,int=0);void SelectCube(const sVector&,const sVector&,sU32,int);
  void SelectSphere(const sVector&,const sVector&,sU32,int);
  void Ring(int,sU32,float,float,int=0);void Extrude(sU32,int=0,int=0);void Subdivide(float=1);
  void FixVertCycle(int);void Crease(int=0,sU32=0,int=0,int=0xfe);void UnCrease(bool=false,int=0xfe);
  void CalcNormals(int=0,int=3);void NeedAllNormals();void Triangulate(int=5,sU32=0,int=0,int=0);
  void DeleteFaces();void ExtrudeNormal(float);
  void Displace(const Texture&,float,float,float);
  void Perlin(const sMatrix&,const sVector&);
  void CubicProjection(const sMatrix&,int=0,int=sGMI_UV0,bool=false);
  void append(const GenMesh&);
  void validate() const;
  std::size_t bytes() const;
};
bool CheckMesh(GenMesh*&,sU32=0x80000000u);
GenMesh* Mesh_DeleteFaces(GenMesh*,int);
GenMesh* Mesh_Cube(int,int,int,int,sFSRT);
GenMesh* Mesh_Cylinder(int,int,int,int,int);
GenMesh* Mesh_Torus(int,int,float,float,float,float,int);
GenMesh* Mesh_Sphere(int,int);
GenMesh* Mesh_SelectCube(GenMesh*,sU32,int,sF323,sF323);
GenMesh* Mesh_SelectSphere(GenMesh*,sU32,int,sF323,sF323);
GenMesh* Mesh_SelectFace(GenMesh*,sU32,int,int);
GenMesh* Mesh_SelectRandom(GenMesh*,sU32,int,sU32,int);
GenMesh* Mesh_Subdivide(KOp*,GenMesh*,int,float,int);
GenMesh* Mesh_Extrude(KOp*,GenMesh*,int,int,int,int,sF323,sF323,sF323);
GenMesh* Mesh_ExtrudeNormal(KOp*,GenMesh*,int,float);
GenMesh* Mesh_DeleteFaces(GenMesh*,int);
GenMesh* Mesh_Multiply2(int,sInt3,sF323,sInt3,sF323,int,sInt3,sF323,std::span<const GenMesh* const>);
GenMesh* Mesh_SmoothAngle(GenMesh*,float);
GenMesh* Mesh_Bend2(GenMesh*,sF323,sF323,float,float);
GenMesh* Mesh_Grid(int,int,int);
GenMesh* Mesh_SelectLogic(GenMesh*,sU32,sU32,sU32,int);
GenMesh* Mesh_Triangulate(GenMesh*,int,int,int);
GenMesh* Mesh_Color(GenMesh*,sF323,sF322,sU32,float,float,int);
GenMesh* Mesh_ShadowEnable(GenMesh*,bool);
GenMesh* Mesh_Bend(KOp*,GenMesh*,int,sFSRT,sFSRT,sF322,sF322,int);
Mesh export_mesh(std::shared_ptr<GenMesh>);
std::shared_ptr<const Mesh> evaluate_kernel(const Project&,const Graph&,std::size_t,const TextureOptions& = {});
} // namespace wz3::legacy
