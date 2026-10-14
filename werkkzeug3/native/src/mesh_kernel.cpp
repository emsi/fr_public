// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "mesh_kernel.hpp"
#include <bit>
#include <cstdarg>
#include <limits>

namespace wz3::legacy {
void sVector::Rotate3(const sMatrix& m,const sVector& v) {
  const float xx=v.x*m.i.x+v.y*m.j.x+v.z*m.k.x;
  const float yy=v.x*m.i.y+v.y*m.j.y+v.z*m.k.y;
  const float zz=v.x*m.i.z+v.y*m.j.z+v.z*m.k.z;
  x=xx;y=yy;z=zz;
}
void sVector::Rotate34(const sMatrix& m,const sVector& v) {
  const auto ww=m.i.w*v.x+m.j.w*v.y+m.k.w*v.z+m.l.w;
  Rotate3(m,v);Add3(m.l);w=ww;
}
void sMatrix::InitEuler(float a,float b,float c) {
  const float sx=sFSin(a),cx=sFCos(a),sy=sFSin(b),cy=sFCos(b),sz=sFSin(c),cz=sFCos(c);
  i.Init(cy*cz,cy*sz,-sy,0);
  j.Init(-cx*sz+sx*sy*cz,cx*cz+sx*sy*sz,sx*cy,0);
  k.Init(sx*sz+cx*sy*cz,-sx*cz+cx*sy*sz,cx*cy,0);
  l.Init(0,0,0,1);
}
void sMatrix::InitEulerPI2(const float* v) {
  if(std::abs(v[0])<1e-6f && std::abs(v[1])<1e-6f && std::abs(v[2])<1e-6f) Init();
  else InitEuler(v[0]*sPI2F,v[1]*sPI2F,v[2]*sPI2F);
}
void sMatrix::InitSRT(const float* v) {InitEulerPI2(v+3);i.Scale3(v[0]);j.Scale3(v[1]);k.Scale3(v[2]);l.Init(v[6],v[7],v[8],1);}
void sMatrix::MulA(const sMatrix& a,const sMatrix& b) {
  sMatrix t;t.Init();t.i.Rotate3(b,a.i);t.j.Rotate3(b,a.j);t.k.Rotate3(b,a.k);t.l.Rotate34(b,a.l);*this=t;
}
void sMatrix::TransR() {
  std::swap(i.y,j.x);std::swap(i.z,k.x);std::swap(j.z,k.y);
  l.Scale3(-1);l.Rotate3(*this);
}
namespace {
thread_local sU32 random_state;
sU32 sGetRnd() {
  const auto a=random_state*0x343fdu+0x269ec3u;
  random_state=a*0x343fdu+0x269ec3u;
  return ((random_state>>10)&0xffffu)|((a<<6)&0xffff0000u);
}
void sSetRndSeed(sU32 seed) {
  const auto product=seed*121u;
  const auto mixed=seed+seed*17u+product+std::uint32_t(std::bit_cast<std::int32_t>(product)/17);
  random_state=mixed;
  for(unsigned i=0;i<4;++i) {if(i)random_state^=mixed;sGetRnd();}
}
}
GenMesh::GenMesh(const GenMesh& m):Vert(m.Vert),Edge(m.Edge),Face(m.Face),Coll(m.Coll),Parts(m.Parts),
  buffer(m.buffer),VertBuf(buffer.data()),GotNormals(m.GotNormals) {}
void GenMesh::Init(sU32 mask,int estimate) {
  if(mask!=sGMF_DEFAULT) throw std::runtime_error("unsupported indexed mesh vertex format");
  Realloc(estimate);Vert.AtLeast(estimate);Edge.AtLeast(estimate*2);Face.AtLeast(estimate/2);
}
void GenMesh::Realloc(int count) {
  if(count<0 || std::size_t(count)>kernel_limit) throw std::runtime_error("indexed mesh vertex limit");
  if(std::size_t(count)*5>=buffer.size()) buffer.resize(std::max(std::size_t(count)*5,std::min(kernel_limit*5,buffer.size()*3/2+5)));
  VertBuf=buffer.data();
}
bool CheckMesh(GenMesh*& mesh,sU32 mask) {
  if(!mesh) throw std::runtime_error("missing indexed mesh input");
  if(!(mask&0x80000000u)) {if(mask)mesh->Mask2Sel(mask);else mesh->All2Sel();}
  return false;
}
GenMesh* Mesh_DeleteFaces(GenMesh* mesh,int mask) {
  CheckMesh(mesh,sU32(mask)<<8);mesh->DeleteFaces();return mesh;
}
void GenMesh::MakeFace(int f,int count,...) {
  if(count<3 || count>256) throw std::runtime_error("indexed mesh face degree");
  std::vector<int> edges(count);
  va_list args;va_start(args,count);
  for(auto& e:edges)e=va_arg(args,int);
  va_end(args);
  Face[f].Init();Face[f].Edge=edges[0];
  auto previous=edges.back();
  for(auto e:edges) {
    if(Edge[e/2].Face[e&1]!=-1 && Face[GetFaceId(e)].Material!=0) throw std::runtime_error("indexed mesh edge already has face");
    Edge[previous/2].Next[previous&1]=e;Edge[e/2].Prev[e&1]=previous;Edge[e/2].Face[e&1]=f;previous=e;
  }
}
void GenMesh::append(const GenMesh& other) {
  const auto v=Vert.Count,e=Edge.Count,f=Face.Count;
  Realloc(v+other.Vert.Count);Vert.AtLeast(v+other.Vert.Count);Edge.AtLeast(e+other.Edge.Count);Face.AtLeast(f+other.Face.Count);
  for(int i=0;i<other.Vert.Count;++i) {
    auto a=other.Vert[i];a.First+=v;a.Next+=v;a.ReIndex=i+v;*Vert.Add()=a;
    for(int k=0;k<5;++k) buffer[(i+v)*5+k]=other.buffer[i*5+k];
  }
  for(int i=0;i<other.Edge.Count;++i) {
    auto a=other.Edge[i];
    for(int j=0;j<2;++j) {a.Vert[j]+=v;a.Face[j]+=f;a.Next[j]+=2*e;a.Prev[j]+=2*e;}
    *Edge.Add()=a;
  }
  for(int i=0;i<other.Face.Count;++i) {auto a=other.Face[i];a.Edge+=2*e;*Face.Add()=a;}
  for(int i=0;i<other.Parts.Count;++i) *Parts.Add()=other.Parts[i]+f;
  GotNormals=false;
}
void GenMesh::validate() const {
  for(int v=0;v<Vert.Count;++v) {
    if(Vert[v].First<0 || Vert[v].First>=Vert.Count || Vert[v].Next<0 || Vert[v].Next>=Vert.Count)
      throw std::runtime_error("invalid indexed mesh vertex cycle");
    for(int k=0;k<5;++k)for(unsigned c=0;c<4;++c)if(!std::isfinite(buffer[v*5+k][c]))
      throw std::runtime_error("non-finite indexed mesh attribute");
  }
  for(int i=0;i<Edge.Count;++i) for(int j=0;j<2;++j) {
    const auto& e=Edge[i];
    if(e.Vert[j]<0 || e.Vert[j]>=Vert.Count || e.Face[j]<0 || e.Face[j]>=Face.Count ||
       e.Next[j]<0 || e.Next[j]>=Edge.Count*2 || e.Prev[j]<0 || e.Prev[j]>=Edge.Count*2)
      throw std::runtime_error("invalid indexed mesh half-edge");
    if(Edge[e.Next[j]/2].Prev[e.Next[j]&1]!=i*2+j || Edge[e.Prev[j]/2].Next[e.Prev[j]&1]!=i*2+j)
      throw std::runtime_error("broken indexed mesh edge cycle");
  }
  for(int f=0;f<Face.Count;++f) {
    if(!Face[f].Material)continue; // Deleted caps retain obsolete face handles.
    const auto start=Face[f].Edge;auto e=start;
    unsigned count=0;
    do {
      if(e<0 || e>=Edge.Count*2 || Edge[e/2].Face[e&1]!=f || ++count>256)
        throw std::runtime_error("invalid indexed mesh face cycle");
      e=Edge[e/2].Next[e&1];
    }while(e!=start);
  }
}
std::size_t GenMesh::bytes() const {
  return Vert.storage.capacity()*sizeof(GenMeshVert)+Edge.storage.capacity()*sizeof(GenMeshEdge)+
    Face.storage.capacity()*sizeof(GenMeshFace)+buffer.capacity()*sizeof(sVector)+Parts.storage.capacity()*sizeof(int);
}

#define sVERIFY(condition) do {if(!(condition)) throw std::runtime_error("mesh kernel invariant: " #condition);} while(false)
#define sPLAYER 1
#define sLINK_KKRIEGER 0
// These warnings belong to the unchanged archive bodies; the host layer still
// builds with the project's full warnings-as-errors policy.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "mesh_kernel.inc"
#pragma GCC diagnostic pop
#undef sVERIFY
#undef sPLAYER
#undef sLINK_KKRIEGER
} // namespace wz3::legacy
