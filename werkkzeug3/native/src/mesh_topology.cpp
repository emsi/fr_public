// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Attribute creases and normal weights from GenMesh::Crease/CalcNormals and
// Mesh_SmoothAngle in ../genmesh.cpp. Topology is independent of UV wedges.
#include "mesh_topology.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <numbers>
#include <stdexcept>
#include <tuple>

namespace wz3 {
namespace {
std::uint32_t next(const Mesh& mesh, std::uint32_t c) {
  return c/4*4 + (c%4+1)%mesh.faces[c/4].count;
}
Vec3 sub(Vec3 a, Vec3 b) { for (unsigned k=0;k<3;++k) a[k]-=b[k]; return a; }
Vec3 cross(Vec3 a, Vec3 b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
float dot(Vec3 a, Vec3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec3 unit(Vec3 v, float length_squared) {
  const auto scale=1.0f/std::sqrt(length_squared);
  for (auto& c:v) c*=scale;
  return v;
}
Vec3 accurate_normal(const Mesh& mesh, const MeshFace& f) {
  Vec3 n{}; float length=0;
  // Preserve CalcFaceNormalAccurate's original loop condition: it stops on a
  // degenerate corner, otherwise uses the last corner, even on nonplanar quads.
  for (unsigned c=0;c<f.count;++c) {
    const auto& p=mesh.vertices[f.vertices[(c+f.count-1)%f.count]].position;
    n=cross(sub(mesh.vertices[f.vertices[c]].position,p),
            sub(mesh.vertices[f.vertices[(c+1)%f.count]].position,p));
    length=dot(n,n);
    if (!(length>1e-40f)) break;
  }
  return length>=1e-40f ? unit(n,length) : Vec3{1,0,0};
}
struct Groups {
  std::vector<std::uint32_t> parents;
  explicit Groups(std::size_t n): parents(n) { std::iota(parents.begin(),parents.end(),0); }
  std::uint32_t root(std::uint32_t i) {
    while (parents[i]!=i) { parents[i]=parents[parents[i]]; i=parents[i]; }
    return i;
  }
  void join(std::uint32_t a, std::uint32_t b) { parents[root(a)]=root(b); }
};
}

std::vector<std::uint32_t> mesh_opposites(const Mesh& mesh) {
  validate_mesh(mesh);
  struct Edge { std::uint32_t lo,hi,corner,start; };
  std::vector<Edge> edges; edges.reserve(mesh.faces.size()*4);
  for (std::uint32_t f=0;f<mesh.faces.size();++f) for (unsigned c=0;c<mesh.faces[f].count;++c) {
    const auto a=mesh_point(mesh,mesh.faces[f].vertices[c]);
    const auto b=mesh_point(mesh,mesh.faces[f].vertices[(c+1)%mesh.faces[f].count]);
    if (a==b) throw std::runtime_error("mesh edge has identical topological endpoints");
    edges.push_back({std::min(a,b),std::max(a,b),f*4+c,a});
  }
  std::sort(edges.begin(),edges.end(),[](const auto& a,const auto& b) {
    return std::tie(a.lo,a.hi,a.corner)<std::tie(b.lo,b.hi,b.corner);
  });
  std::vector<std::uint32_t> opposite(mesh.faces.size()*4,mesh_no_point);
  for (std::size_t i=0;i<edges.size();) {
    auto j=i+1;
    while (j<edges.size() && edges[j].lo==edges[i].lo && edges[j].hi==edges[i].hi) ++j;
    if (j-i>2 || (j-i==2 && edges[i].start==edges[i+1].start))
      throw std::runtime_error("non-manifold or inconsistently wound mesh topology");
    if (j-i==2) {
      opposite[edges[i].corner]=edges[i+1].corner;
      opposite[edges[i+1].corner]=edges[i].corner;
    }
    i=j;
  }
  return opposite;
}

void crease_mesh_seams(Mesh& mesh, std::uint8_t face_boundary_flags) {
  const auto opposite=mesh_opposites(mesh);
  for (std::uint32_t c=0;c<opposite.size();++c) {
    const auto d=opposite[c];
    if (d==mesh_no_point || c>d) continue;
    auto& a=mesh.faces[c/4]; auto& b=mesh.faces[d/4];
    std::uint8_t flags=a.mask!=b.mask ? face_boundary_flags : 0;
    if (mesh.vertices[a.vertices[c%4]].uv!=mesh.vertices[b.vertices[next(mesh,d)%4]].uv ||
        mesh.vertices[b.vertices[d%4]].uv!=mesh.vertices[a.vertices[next(mesh,c)%4]].uv) flags|=crease_uv;
    a.crease[c%4]|=flags; b.crease[d%4]|=flags;
  }
}

void smooth_mesh_angle(Mesh& mesh, float angle) {
  const auto opposite=mesh_opposites(mesh);
  std::vector<Vec3> normals;
  for (const auto& f:mesh.faces) normals.push_back(accurate_normal(mesh,f));
  const auto threshold=std::cos(angle*(2*std::numbers::pi_v<float>)*0.25f);
  for (std::uint32_t c=0;c<opposite.size();++c) {
    const auto d=opposite[c];
    if (d==mesh_no_point || c>d) continue;
    if (std::abs(dot(normals[c/4],normals[d/4]))<threshold) {
      mesh.faces[c/4].crease[c%4]|=crease_normal|crease_tangent;
      mesh.faces[d/4].crease[d%4]|=crease_normal|crease_tangent;
    }
  }
}

MeshNormals mesh_normals(const Mesh& mesh) {
  const auto opposite=mesh_opposites(mesh);
  Groups groups(opposite.size());
  for (std::uint32_t c=0;c<opposite.size();++c) {
    const auto d=opposite[c];
    if (d==mesh_no_point || c>d) continue;
    if (!((mesh.faces[c/4].crease[c%4]|mesh.faces[d/4].crease[d%4])&crease_normal)) {
      groups.join(c,next(mesh,d)); groups.join(next(mesh,c),d);
    }
  }
  std::vector<Vec3> sum(opposite.size()), center(opposite.size());
  std::vector<bool> initialized(opposite.size());
  for (std::uint32_t f=0;f<mesh.faces.size();++f) for (unsigned c=0;c<mesh.faces[f].count;++c) {
    const auto& face=mesh.faces[f];
    const auto& p=mesh.vertices[face.vertices[c]].position;
    const auto a=sub(mesh.vertices[face.vertices[(c+1)%face.count]].position,p);
    const auto b=sub(mesh.vertices[face.vertices[(c+face.count-1)%face.count]].position,p);
    const auto group=groups.root(f*4+c);
    if (initialized[group] && center[group]!=p)
      throw std::runtime_error("normal group contains separated position wedges");
    initialized[group]=true; center[group]=p;
    const auto weight=dot(a,a)*dot(b,b);
    if (weight>1e-20f) {
      const auto n=cross(a,b);
      for (unsigned k=0;k<3;++k) sum[group][k]+=n[k]/weight;
    }
  }
  for (auto& n:sum) {
    const auto length=dot(n,n);
    if (!std::isfinite(length)) throw std::runtime_error("mesh normal arithmetic overflow");
    n=length>1e-20f ? unit(n,length) : Vec3{1,0,0}; // sVector::UnitSafe3
  }
  MeshNormals out(mesh.faces.size());
  for (std::uint32_t f=0;f<mesh.faces.size();++f) for (unsigned c=0;c<mesh.faces[f].count;++c)
    out[f][c]=sum[groups.root(f*4+c)];
  return out;
}
} // namespace wz3
