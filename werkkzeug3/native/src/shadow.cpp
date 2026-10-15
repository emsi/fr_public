// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// EngMesh::PrepareJob/UpdateShadowCacheJob and sFrustum::ZFailVolume.
#include "wz3/shadow.hpp"
#include "mesh_kernel.hpp"
#include "minmesh_kernel.hpp"
#include "mesh_topology.hpp"
#include <cmath>
#include <set>
namespace wz3 {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Vec3 sub(Vec3 a,Vec3 b){for(unsigned c=0;c<3;++c)a[c]-=b[c];return a;}
float dot(Vec3 a,Vec3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec3 cross(Vec3 a,Vec3 b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
ShadowPlane plane(Vec3 a,Vec3 b,Vec3 c){const auto n=cross(sub(b,a),sub(c,a));return {n[0],n[1],n[2],-dot(n,a)};}
float distance(const ShadowPlane& p,Vec3 v){return p[0]*v[0]+p[1]*v[1]+p[2]*v[2]+p[3];}
struct Polygon {std::vector<std::uint32_t> vertices,edges,neighbors;};
}
std::vector<std::uint8_t> shadow_face_mask(const Mesh& mesh,const std::map<std::size_t,Material>& materials) {
  const auto casts=[&](std::optional<std::size_t> binding){
    if(!binding)return false;
    const auto found=materials.find(*binding);require(found!=materials.end(),"missing shadow material snapshot");
    for(const auto& p:found->second.passes)if(p.program==MaterialProgram::shadow)return true;
    return false;
  };
  std::vector<std::uint8_t> mask;
  if(mesh.min_kernel)for(int f=0;f<mesh.min_kernel->Faces.Count;++f) {
    const auto& face=mesh.min_kernel->Faces[f];const auto* m=mesh.min_kernel->Clusters[face.Cluster].Mtrl;
    mask.push_back(face.Count>=3 && face.Cluster && !(face.Flags&1) && m && casts(m->binding));
  }
  else if(mesh.kernel)for(int f=0;f<mesh.kernel->Face.Count;++f){const auto& face=mesh.kernel->Face[f];mask.push_back(face.Material && face.Used && casts(face.binding));}
  else for(const auto& face:mesh.faces)mask.push_back(face.casts_shadow && casts(face.material));
  return mask;
}
ShadowMesh prepare_shadow_mesh(const Mesh& mesh,std::span<const std::uint8_t> mask) {
  validate_mesh(mesh);std::vector<Polygon> polygons;
  if(mesh.min_kernel) {
    // EngMesh::PrepareJob(GenMinMesh*) uses its own face normals, welded
    // adjacency and zero mapping for unselected neighboring face planes.
    const auto& core=*mesh.min_kernel;require(mask.size()==std::size_t(core.Faces.Count),"MINMESH shadow face mask size");
    ShadowMesh out;out.planes.push_back({});std::vector<std::uint32_t> mapping(mask.size());
    for(std::size_t i=0;i<mask.size();++i)if(mask[i]) {
      const auto& f=core.Faces[i];require(f.Count>=3,"invalid MINMESH shadow polygon");
      mapping[i]=out.planes.size();const auto& n=f.Normal;const auto& p=core.Vertices[f.Vertices[0]].Pos;
      if(mesh.animation_source) {
        // Job::UpdatePlanes uses the first triangle of each posed polygon,
        // without normalization. The archived adjacency remains unchanged.
        out.planes.push_back(plane(mesh.vertices[f.Vertices[0]].position,mesh.vertices[f.Vertices[1]].position,mesh.vertices[f.Vertices[2]].position));
      }else out.planes.push_back({n.x,n.y,n.z,-n.Dot(p)});
    }
    if(out.planes.size()==1)return out;
    for(const auto& v:mesh.vertices)out.vertices.push_back(v.position);
    for(std::size_t i=0;i<mask.size();++i)if(mask[i]) {
      const auto& f=core.Faces[i];
      for(unsigned c=2;c<f.Count;++c) {
        for(auto v:{f.Vertices[0],f.Vertices[c-1],f.Vertices[c]})out.triangles.push_back(v*2);
        out.triangle_planes.push_back(mapping[i]);
      }
      for(unsigned c=0;c<f.Count;++c) {
        const auto adjacent=f.Adjacent[c];const auto neighbor=adjacent<0 ? -1 : adjacent/8;
        require(neighbor<int(mask.size()),"MINMESH shadow adjacency out of bounds");
        // Preserve the archive's face-index comparison: open boundary edges
        // (neighbor -1) do not produce silhouette edges in this engine path.
        if(int(i)<neighbor)out.edges.push_back({{std::uint32_t(f.Vertices[c]*2),std::uint32_t(f.Vertices[(c+1)%f.Count]*2)},
          {mapping[i],mapping[neighbor]}});
      }
    }
    return out;
  }
  if(mesh.kernel) {
    const auto& core=*mesh.kernel;require(mask.size()==std::size_t(core.Face.Count),"shadow kernel face mask size");polygons.resize(mask.size());
    for(std::uint32_t f=0;f<polygons.size();++f) {
      if(!mask[f])continue;
      auto& p=polygons[f];const auto start=core.Face[f].Edge;auto e=start;
      do {const auto& edge=core.Edge[e/2];p.vertices.push_back(edge.Vert[e&1]);p.edges.push_back(e/2);const auto other=edge.Face[(e&1)^1];p.neighbors.push_back(other<0 ? mesh_no_point : std::uint32_t(other));e=edge.Next[e&1];require(p.vertices.size()<=256,"shadow polygon degree limit");}while(e!=start);
    }
  }else {
    require(mask.size()==mesh.faces.size(),"shadow face mask size");const auto opposite=mesh_opposites(mesh);polygons.resize(mesh.faces.size());
    for(std::uint32_t f=0;f<polygons.size();++f)for(unsigned c=0;c<mesh.faces[f].count;++c) {
      const auto edge=f*4+c,other=opposite[edge];auto& p=polygons[f];p.vertices.push_back(mesh.faces[f].vertices[c]);p.edges.push_back(std::min(edge,other));p.neighbors.push_back(other==mesh_no_point ? mesh_no_point : other/4);
    }
  }
  ShadowMesh out;out.planes.push_back({});std::vector<std::uint32_t> mapping(polygons.size());
  // Preserve PrepareJob's Temp+1 face numbering, including skipped faces. A
  // dangling legacy plane reference is rejected instead of reading past memory.
  for(std::size_t f=0;f<polygons.size();++f) {
    mapping[f]=std::uint32_t(out.planes.size());if(!mask[f])continue;
    const auto& p=polygons[f];require(p.vertices.size()>=3,"invalid shadow polygon");Vec3 n{};float length=0;
    for(std::size_t c=0;c<p.vertices.size();++c) {
      const auto a=mesh.vertices[p.vertices[(c+p.vertices.size()-1)%p.vertices.size()]].position;
      n=cross(sub(mesh.vertices[p.vertices[c]].position,a),sub(mesh.vertices[p.vertices[(c+1)%p.vertices.size()]].position,a));length=dot(n,n);if(!(length>1e-40f))break;
    }
    if(length>=1e-40f){const auto scale=1.f/std::sqrt(length);for(auto& c:n)c*=scale;}else n={1,0,0};
    out.planes.push_back({n[0],n[1],n[2],-dot(n,mesh.vertices[p.vertices[0]].position)});
  }
  if(out.planes.size()==1)return out;
  // Retain vertex order; unused vertices cost storage but cannot alter winding.
  out.vertices.reserve(mesh.vertices.size());for(const auto& v:mesh.vertices)out.vertices.push_back(v.position);
  std::set<std::uint32_t> seen;
  for(std::size_t f=0;f<polygons.size();++f)if(mask[f]) {
    const auto& p=polygons[f];
    for(std::size_t c=1;c+1<p.vertices.size();++c){for(auto v:{p.vertices[0],p.vertices[c],p.vertices[c+1]})out.triangles.push_back(v*2);out.triangle_planes.push_back(mapping[f]);}
    for(std::size_t c=0;c<p.vertices.size();++c)if(seen.insert(p.edges[c]).second) {
      const auto neighbor=p.neighbors[c];const auto other=neighbor==mesh_no_point ? 0 : mapping.at(neighbor);
      require(other<out.planes.size(),"archived shadow edge references an uninitialized face plane");
      out.edges.push_back({{p.vertices[c]*2,p.vertices[(c+1)%p.vertices.size()]*2},{mapping[f],other}});
    }
  }
  return out;
}
ShadowIndices shadow_indices(const ShadowMesh& mesh,const Vec3& light) {
  for(auto c:light)require(std::isfinite(c),"nonfinite shadow light position");
  std::vector<bool> front;front.reserve(mesh.planes.size());
  for(const auto& p:mesh.planes)front.push_back(distance(p,light)>=0);
  ShadowIndices out;out.indices.reserve(mesh.edges.size()*6+mesh.triangles.size());
  for(const auto& edge:mesh.edges)if(front.at(edge.planes[0])!=front.at(edge.planes[1])) {
    const auto k=unsigned(front[edge.planes[0]]),a=edge.vertices[k^1],b=edge.vertices[k];
    for(auto i:{a,b,b+1,a,b+1,a+1})out.indices.push_back(i);
  }
  out.silhouette_count=out.indices.size();
  for(std::size_t t=0;t<mesh.triangle_planes.size();++t)for(unsigned c=0;c<3;++c)out.indices.push_back(mesh.triangles[t*3+c]+unsigned(front.at(mesh.triangle_planes[t])));
  return out;
}
bool shadow_box_culled(const Bounds& box,std::span<const ShadowPlane> planes) {
  for(const auto& p:planes){Vec3 v;for(unsigned c=0;c<3;++c)v[c]=p[c]>0 ? box.max[c] : box.min[c];if(distance(p,v)<-1e-6f)return true;}return false;
}
std::vector<ShadowPlane> shadow_near_volume(const SceneCamera& camera,const Vec3& light,const std::array<float,4>& rect) {
  require(camera.zoom_x!=0 && camera.zoom_y!=0,"invalid shadow camera zoom");const auto& m=camera.world;std::array<Vec3,4> points;
  const auto xs=camera.near_clip/camera.zoom_x,ys=camera.near_clip/camera.zoom_y;
  for(unsigned i=0;i<4;++i)for(unsigned c=0;c<3;++c){auto& v=points[i][c];v=m[12+c];v+=m[c]*(xs*rect[(i&1) ? 2 : 0]);v+=m[4+c]*(ys*rect[(i&2) ? 3 : 1]);v+=m[8+c]*camera.near_clip;}
  std::vector<ShadowPlane> planes{plane(points[0],points[1],points[2])};const auto d=distance(planes[0],light);
  if(std::abs(d)>=1e-4f) {
    planes.push_back(plane(light,points[1],points[0]));planes.push_back(plane(light,points[3],points[1]));planes.push_back(plane(light,points[2],points[3]));planes.push_back(plane(light,points[0],points[2]));
    if(d<0)for(auto& p:planes)for(auto& c:p)c=-c;
    Vec3 n;for(unsigned c=0;c<3;++c){n[c]=m[12+c]-light[c];n[c]+=m[8+c]*camera.near_clip;}planes.push_back({n[0],n[1],n[2],-dot(n,light)});
  }else {auto p=planes[0];for(auto& c:p)c=-c;planes.push_back(p);planes[0][3]+=1e-4f;planes[1][3]-=1e-4f;}
  return planes;
}
} // namespace wz3
