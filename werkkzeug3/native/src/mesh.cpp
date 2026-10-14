// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Static geometry formulas and masks from ../genmesh.cpp; SRT from _types.cpp.
#include "wz3/mesh.hpp"
#include "mesh_topology.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <stdexcept>
#include <unordered_map>

namespace wz3 {
namespace {
constexpr std::size_t element_limit = 1 << 20;
constexpr float tau = 2.0f * std::numbers::pi_v<float>;
using Matrix = std::array<Vec3, 4>; // basis columns and translation
constexpr Matrix identity{{{1,0,0}, {0,1,0}, {0,0,1}, {0,0,0}}};
float number(const Operator& op, std::size_t index) {
  const auto value = as_float(op.parameters.at(index));
  if (!std::isfinite(value) || std::abs(value) > 1000000) throw std::runtime_error("invalid mesh parameter");
  return value;
}
void require(const Operator& op, std::size_t count) {
  if (op.parameters.size() != count) throw std::runtime_error("unsupported mesh parameter layout");
}
unsigned tessellation(std::uint32_t value, unsigned minimum = 1) {
  if (value < minimum || value > 256) throw std::runtime_error("mesh tessellation outside supported range");
  return value;
}
Vec3 transform(const Matrix& m, Vec3 p, bool translation = true) {
  Vec3 out{};
  for (unsigned i = 0; i < 3; ++i) out[i] = p[0]*m[0][i] + p[1]*m[1][i] + p[2]*m[2][i] + (translation ? m[3][i] : 0);
  return out;
}
Matrix multiply(const Matrix& a, const Matrix& b) {
  Matrix result{};
  for (unsigned i = 0; i < 4; ++i) result[i] = transform(b, a[i], i == 3);
  return result;
}
Matrix euler(Vec3 turns) {
  if (std::abs(turns[0]) < 1e-6f && std::abs(turns[1]) < 1e-6f && std::abs(turns[2]) < 1e-6f) return identity;
  Vec3 s{}, c{};
  for (unsigned i = 0; i < 3; ++i) { const float a = turns[i] * tau; s[i] = std::sin(double(a)); c[i] = std::cos(double(a)); }
  return {{{c[1]*c[2], c[1]*s[2], -s[1]},
           {-c[0]*s[2]+s[0]*s[1]*c[2], c[0]*c[2]+s[0]*s[1]*s[2], s[0]*c[1]},
           {s[0]*s[2]+c[0]*s[1]*c[2], -s[0]*c[2]+c[0]*s[1]*s[2], c[0]*c[1]}, {0,0,0}}};
}
Matrix srt(const Operator& op, unsigned start) {
  auto m = euler({number(op,start+3), number(op,start+4), number(op,start+5)});
  for (unsigned i = 0; i < 3; ++i) {
    for (auto& value : m[i]) value *= number(op,start+i);
    m[3][i] = number(op,start+6+i);
  }
  return m;
}
void reserve(Mesh& mesh, std::size_t vertices, std::size_t faces) {
  if (vertices > element_limit || faces > element_limit) throw std::runtime_error("mesh exceeds one-million element limit");
  mesh.vertices.reserve(vertices); mesh.faces.reserve(faces);
}
std::uint32_t vertex(Mesh& mesh, Vec3 p, Vec3 uv, unsigned mask = 0, std::uint32_t point = mesh_no_point) {
  if (mesh.vertices.size() == element_limit) throw std::runtime_error("mesh vertex limit exceeded");
  mesh.vertices.push_back({p, uv, static_cast<std::uint8_t>(mask), point == mesh_no_point ? std::uint32_t(mesh.vertices.size()) : point});
  return mesh.vertices.size() - 1;
}
void face(Mesh& mesh, std::array<std::uint32_t, 4> corners, unsigned count = 4, unsigned mask = 0) {
  if (mesh.faces.size() == element_limit) throw std::runtime_error("mesh face limit exceeded");
  mesh.faces.push_back({corners, count, static_cast<std::uint8_t>(mask), {}, 0});
}
Vec3 normal(const Mesh& mesh, const MeshFace& f) {
  const auto& a = mesh.vertices[f.vertices[f.count-1]].position;
  const auto& b = mesh.vertices[f.vertices[0]].position;
  const auto& c = mesh.vertices[f.vertices[1]].position;
  const Vec3 u{b[0]-a[0], b[1]-a[1], b[2]-a[2]}, v{c[0]-a[0], c[1]-a[1], c[2]-a[2]};
  return {u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]};
}
Vec3 project_uv(Vec3 p, unsigned axis, float sign) {
  if (axis == 0) return {sign*p[2], -p[1], 0};
  if (axis == 1) return {p[0], -sign*p[2], 0};
  return {-sign*p[0], -p[1], 0};
}
Mesh cube(const Operator& op) {
  require(op, 13);
  const unsigned t[] = {tessellation(op.parameters[0]), tessellation(op.parameters[1]), tessellation(op.parameters[2])};
  const auto flags = op.parameters[3];
  if (flags & ~15u) throw std::runtime_error("cube collision flags are not ported");
  Mesh mesh;
  reserve(mesh, 2*((t[0]+1)*(t[1]+1)+(t[0]+1)*(t[2]+1)+(t[1]+1)*(t[2]+1)),
          2*(t[0]*t[1]+t[0]*t[2]+t[1]*t[2]));
  const auto matrix = srt(op, 4);
  const Vec3 uv_scale = flags & 8 ? Vec3{number(op,4),number(op,5),number(op,6)} : Vec3{1,1,1};
  std::unordered_map<unsigned,std::uint32_t> points;
  for (unsigned side = 0; side < 6; ++side) {
    const unsigned axis = side / 2, u = axis == 0 ? 2 : 0, v = axis == 1 ? 2 : 1;
    const float sign = side & 1 ? 1 : -1;
    const float direction = axis == 0 ? -sign : axis == 1 ? 1 : sign;
    const float vdirection = axis == 1 ? -sign : 1;
    const auto first = mesh.vertices.size();
    for (unsigned y = 0; y <= t[v]; ++y) for (unsigned x = 0; x <= t[u]; ++x) {
      unsigned coordinate[3]{}; coordinate[axis] = sign > 0 ? t[axis] : 0;
      coordinate[u] = direction > 0 ? x : t[u]-x;
      coordinate[v] = vdirection > 0 ? y : t[v]-y;
      const auto key = (coordinate[0]*(t[1]+1)+coordinate[1])*(t[2]+1)+coordinate[2];
      const auto point = points.try_emplace(key,mesh.vertices.size()).first->second;
      Vec3 p{};
      for (unsigned c=0;c<3;++c) p[c]=float(coordinate[c])/t[c]-0.5f;
      Vec3 scaled{p[0]*uv_scale[0], p[1]*uv_scale[1], p[2]*uv_scale[2]};
      auto uv = project_uv(scaled, axis, sign);
      uv[0] += std::abs(uv_scale[u]) * 0.5f; uv[1] += std::abs(uv_scale[v]) * 0.5f;
      if (flags & 2) {
        if (side == 4) uv[0] += uv_scale[2];
        if (side == 1) uv[0] += uv_scale[2] + uv_scale[0];
        if (side == 5) uv[0] += 2*uv_scale[2] + uv_scale[0];
      }
      if (flags & 4) p[1] += 0.5f;
      vertex(mesh, transform(matrix,p), uv, 1u << side, point);
    }
    for (unsigned y = 0; y < t[v]; ++y) for (unsigned x = 0; x < t[u]; ++x) {
      const auto a = std::uint32_t(first + y*(t[u]+1) + x), b = a + t[u] + 1;
      face(mesh, {a,a+1,b+1,b}, 4, 1u << side);
    }
  }
  crease_mesh_seams(mesh, flags & 1 ? 0xfe : crease_uv);
  return mesh;
}
void cap(Mesh& mesh, std::span<const Vec3> ring, bool reverse, unsigned mask, std::uint32_t first_point) {
  std::vector<std::uint32_t> corners;
  Vec3 center{};
  for (const auto& p : ring) {
    corners.push_back(vertex(mesh,p,{p[0]+0.5f, (reverse ? -p[2] : p[2])+0.5f,0}, mask, first_point+corners.size()));
    for (unsigned c = 0; c < 3; ++c) center[c] += p[c] / ring.size();
  }
  if (ring.size() == 3) {
    face(mesh, reverse ? std::array<std::uint32_t,4>{corners[2],corners[1],corners[0],0} :
                        std::array<std::uint32_t,4>{corners[0],corners[1],corners[2],0}, 3, mask);
  } else {
    const auto mid = vertex(mesh,center,{center[0]+0.5f,(reverse ? -center[2] : center[2])+0.5f,0},mask);
    for (unsigned i = 0; i < ring.size(); ++i) {
      auto a = corners[i], b = corners[(i+1)%ring.size()];
      if (reverse) std::swap(a,b);
      face(mesh,{mid,a,b,0},3,mask);
    }
  }
}
Mesh cylinder(const Operator& op) {
  if (op.parameters.size() != 4 && op.parameters.size() != 5) throw std::runtime_error("unsupported Cylinder layout");
  const auto tx = tessellation(op.parameters[0],3), ty = tessellation(op.parameters[1]);
  const auto mode = op.parameters[2];
  if (mode > 3 || op.parameters[3] != 1 || (op.parameters.size() == 5 && op.parameters[4]))
    throw std::runtime_error("Cylinder radial subdivisions/arcs are not ported");
  Mesh mesh; reserve(mesh,(tx+1)*(ty+1)+2*(tx+1),tx*ty+2*tx);
  std::vector<Vec3> ring(tx);
  for (unsigned x = 0; x < tx; ++x) {
    const float angle = x*tau/tx;
    ring[x] = {float(std::sin(double(angle))*0.5),0,float(-std::cos(double(angle))*0.5)};
  }
  for (unsigned y = 0; y <= ty; ++y) for (unsigned x = 0; x <= tx; ++x) {
    auto p = ring[x%tx]; p[1] = 0.5f - float(y)/ty + ((mode & 2) ? 0.5f : 0);
    vertex(mesh,p,{float(x)/tx,float(y)/ty,0},(y == 0 || y == ty) ? 1 : 0,y*(tx+1)+x%tx);
  }
  for (unsigned y = 0; y < ty; ++y) for (unsigned x = 0; x < tx; ++x) {
    const auto a = y*(tx+1)+x, b = a+tx+1;
    face(mesh,{a,a+1,b+1,b});
  }
  if (!(mode & 1)) for (unsigned bottom = 0; bottom < 2; ++bottom) {
    for (auto& p : ring) p[1] = 0.5f - bottom + ((mode & 2) ? 0.5f : 0);
    cap(mesh,ring,!bottom,1,bottom*ty*(tx+1));
  }
  crease_mesh_seams(mesh,0xfe);
  return mesh;
}
Mesh torus(const Operator& op) {
  require(op,7);
  const auto tx = tessellation(op.parameters[0],3), ty = tessellation(op.parameters[1],3);
  auto ro = number(op,2), ri = number(op,3);
  const auto phase = number(op,4), arc = number(op,5);
  const auto flags = op.parameters[6];
  const bool closed=arc==1;
  if (arc<=0 || arc>1 || flags>3 || (!closed && ty>4))
    throw std::runtime_error("Torus needs arc in (0,1] and open caps of three or four sides");
  if (flags & 1) { ri = (ro-ri)*0.5f; ro -= ri; }
  Mesh mesh; reserve(mesh,(tx+1)*(ty+1)+(closed ? 0 : 2*ty),tx*ty+(closed ? 0 : 2));
  std::vector<Vec3> ring(ty);
  const float start = phase*(2.0*std::numbers::pi_v<double>)/ty;
  for (unsigned y = 0; y < ty; ++y) {
    const float angle = start + y*tau/ty;
    ring[y] = {float(std::sin(double(angle))*ri)-ro,float(-std::cos(double(angle))*ri),0};
  }
  const auto original = ring;
  const auto step = euler({0,arc/tx,0});
  for (unsigned x = 0; x <= tx; ++x) {
    for (unsigned y = 0; y <= ty; ++y) {
      auto p = (closed && x == tx ? original : ring)[y%ty];
      if (flags & 2) p[1] += ri;
      vertex(mesh,p,{float(y)/ty,float(x)/tx,0},0,(closed ? x%tx : x)*(ty+1)+y%ty);
    }
    for (auto& p : ring) p = transform(step,p);
  }
  for (unsigned x = 0; x < tx; ++x) for (unsigned y = 0; y < ty; ++y) {
    const auto a = x*(ty+1)+y, b = a+ty+1;
    face(mesh,{a,a+1,b+1,b});
  }
  if (!closed) for (unsigned end=0;end<2;++end) {
    std::array<std::uint32_t,4> corners{};
    for (unsigned y=0;y<ty;++y) {
      const auto id=end*tx*(ty+1)+y;
      const auto& v=mesh.vertices[id];
      // Preserve the source's unfinished cap UV projection: V is constant,
      // U follows the unsplit ring. Only the side surface wraps U to one.
      corners[end ? y : ty-1-y]=vertex(mesh,v.position,v.uv,0,mesh_point(mesh,id));
    }
    face(mesh,corners,ty);
    mesh.faces.back().crease.fill(crease_uv);
  }
  crease_mesh_seams(mesh,0);
  return mesh;
}
Mesh sphere(const Operator& op) {
  require(op,2);
  // The archived three-sided case overwrites ring UVs while looking for cap
  // centers. Keep that legacy corner case explicit until its layout is ported.
  const auto tx=tessellation(op.parameters[0],4), ty=tessellation(op.parameters[1]);
  Mesh mesh; reserve(mesh,(tx+1)*(ty+1)+2,tx*(ty+2));
  for (unsigned y=0;y<=ty;++y) {
    const float latitude=(y+0.5f)*std::numbers::pi_v<float>/(ty+1);
    const auto radius=float(std::sin(double(latitude)))*0.5f;
    const auto height=float(-0.5*std::cos(double(latitude)));
    for (unsigned x=0;x<=tx;++x) {
      const float angle=(x%tx)*tau/tx;
      vertex(mesh,{float(std::sin(double(angle)))*radius,height,float(-std::cos(double(angle)))*radius},
             {float(x)/tx,1.0f-float(y)/ty,0},0,y*(tx+1)+x%tx);
    }
  }
  for (unsigned y=0;y<ty;++y) for (unsigned x=0;x<tx;++x) {
    const auto a=y*(tx+1)+x, b=a+tx+1;
    face(mesh,{a,b,b+1,a+1});
  }
  for (unsigned top=0;top<2;++top) {
    const auto start=top*ty*(tx+1);
    Vec3 center{};
    for (unsigned x=0;x<tx;++x) for (unsigned c=0;c<3;++c) center[c]+=mesh.vertices[start+x].position[c]*(1.0f/tx);
    // Triangulate averages the end ring: the poles stop short of +/-0.5.
    // Edge2Vert shifts the unsplit cap center's U from zero to one.
    const auto mid=vertex(mesh,center,{1,float(1-top),0});
    for (unsigned x=0;x<tx;++x) {
      auto a=start+x,b=a+1; if (top) std::swap(a,b);
      face(mesh,{mid,a,b,0},3,1);
    }
  }
  crease_mesh_seams(mesh,0); // The caps are smooth; only the U seam is creased.
  return mesh;
}
void append(Mesh& dest, const Mesh& source) {
  reserve(dest,dest.vertices.size()+source.vertices.size(),dest.faces.size()+source.faces.size());
  const auto offset = dest.vertices.size();
  for (std::uint32_t i=0;i<source.vertices.size();++i) {
    auto v=source.vertices[i]; v.point=mesh_point(source,i)+offset; dest.vertices.push_back(v);
  }
  for (auto f : source.faces) {
    for (unsigned i = 0; i < f.count; ++i) f.vertices[i] += offset;
    dest.faces.push_back(f);
  }
}
void apply_mask(std::uint8_t& value, unsigned bits, bool selected, unsigned mode) {
  if ((mode == 0 && selected) || (mode == 2 && selected) || (mode == 3 && !selected)) value |= bits;
  if ((mode == 1 && selected) || (mode == 2 && !selected) || (mode == 3 && selected)) value &= ~bits;
}
void select_cube(Mesh& mesh, const Operator& op) {
  require(op,8);
  const auto mask = op.parameters[0], mode = op.parameters[1];
  if ((mask & 0xff0000ff) || mode > 7) throw std::runtime_error("edge selection masks are not ported");
  std::vector<bool> selected(mesh.vertices.size()), faces(mesh.faces.size());
  for (std::size_t i = 0; i < selected.size(); ++i) {
    selected[i] = true;
    for (unsigned c = 0; c < 3; ++c)
      selected[i] = selected[i] && std::abs(mesh.vertices[i].position[c]-number(op,2+c)) <= number(op,5+c)*0.5f;
  }
  for (std::size_t i = 0; i < faces.size(); ++i) {
    faces[i] = true;
    for (unsigned k = 0; k < mesh.faces[i].count; ++k) faces[i] = faces[i] && selected[mesh.faces[i].vertices[k]];
  }
  if (mode & 4) {
    std::fill(selected.begin(),selected.end(),false);
    for (std::size_t i = 0; i < faces.size(); ++i) if (faces[i])
      for (unsigned k = 0; k < mesh.faces[i].count; ++k) selected[mesh.faces[i].vertices[k]] = true;
  }
  for (std::size_t i = 0; i < selected.size(); ++i) apply_mask(mesh.vertices[i].mask,(mask>>16)&255,selected[i],mode&3);
  for (std::size_t i = 0; i < faces.size(); ++i) apply_mask(mesh.faces[i].mask,(mask>>8)&255,faces[i],mode&3);
}
void bend(Mesh& mesh, const Operator& op) {
  require(op,8);
  auto forward=euler({number(op,3),number(op,4),number(op,5)});
  const Vec3 center{number(op,0),number(op,1),number(op,2)};
  for (unsigned c=0;c<3;++c) forward[3][c]=-center[c];
  auto inverse=identity;
  for (unsigned a=0;a<3;++a) for (unsigned b=0;b<3;++b) inverse[a][b]=forward[b][a];
  inverse[3]=transform(inverse,center,false); // sMatrix::TransR
  const auto length=number(op,6), angle=number(op,7)*tau;
  if (length<=0) throw std::runtime_error("Bend 2 requires positive length");
  for (auto& v:mesh.vertices) {
    auto p=transform(forward,v.position);
    const auto height=p[1];
    if (height>=0) p[1]-=std::min(height,length);
    const auto t=std::clamp(height/length,0.0f,1.0f)*angle;
    const float s=std::sin(double(t)),c=std::cos(double(t));
    const auto x=p[0],y=p[1];
    p[0]=c*x-s*y; p[1]=s*x+c*y;
    v.position=transform(inverse,p);
  }
}

class Evaluator {
public:
  Evaluator(const Project& project, const Graph& graph) : project_(project), graph_(graph), cache_(project.operators.size()), visiting_(cache_.size()) {}
  std::shared_ptr<const Mesh> evaluate(std::size_t id, unsigned depth = 0) {
    if (id >= cache_.size() || !graph_.nodes.at(id).active) throw std::runtime_error("invalid or inactive mesh operator");
    if (cache_[id]) return cache_[id];
    if (depth > 512 || visiting_[id]) throw std::runtime_error("cycle or excessive depth in mesh graph");
    visiting_[id] = true;
    const auto& op = project_.operators[id]; const auto& node = graph_.nodes[id];
    if (!op.animations.empty()) throw std::runtime_error("animated mesh operators are not ported");
    const auto input = [&]() {
      if (node.inputs.size() != 1) throw std::runtime_error("mesh operator requires one input");
      return evaluate(node.inputs[0],depth+1);
    };
    if (op.command == 1) {
      if (node.links.size() != 1 || !node.links[0]) throw std::runtime_error("unresolved mesh Load");
      cache_[id] = evaluate(*node.links[0],depth+1);
    } else if (op.command == 2 || op.command == 3) cache_[id] = input();
    else {
      auto mesh = std::make_shared<Mesh>();
      if (op.command >= 0x81 && op.command <= 0x84) {
        if (!node.inputs.empty()) throw std::runtime_error("mesh primitive has unexpected inputs");
        *mesh = op.command == 0x81 ? cube(op) : op.command == 0x82 ? cylinder(op) : op.command == 0x83 ? torus(op) : sphere(op);
      } else if (op.command == 0x92) {
        require(op,0);
        if (node.inputs.empty()) throw std::runtime_error("mesh Add requires inputs");
        for (const auto source : node.inputs) append(*mesh,*evaluate(source,depth+1));
      } else if (op.command == 0x86 || op.command == 0x88 || op.command == 0x89 || op.command == 0x96 || op.command == 0xa3 || op.command == 0xa5 || op.command == 0xa6 || op.command == 0x95 || op.command == 0xab || op.command == 0xac) {
        *mesh = *input();
        if (op.command == 0x86) select_cube(*mesh,op);
        else if (op.command == 0x88 || op.command == 0x89) {
          require(op,op.command == 0x88 ? 10 : 12);
          const auto mask = op.parameters[0], src = op.command == 0x88 ? 0 : op.parameters[10], dst = op.command == 0x88 ? 0 : op.parameters[11];
          if (mask > 255 || (src != 0 && src != 5) || (dst != 0 && dst != 5)) throw std::runtime_error("mesh transform attribute/mask is not ported");
          const auto matrix = srt(op,1);
          for (auto& v : mesh->vertices) if (!mask || (v.mask & mask))
            (dst == 0 ? v.position : v.uv) = transform(matrix,src == 0 ? v.position : v.uv);
        } else if (op.command == 0x96) {
          require(op,2);
          if (op.parameters[0] > 255 || node.links.size() != 1 || !node.links[0] || !is_material(*node.links[0]))
            throw std::runtime_error("invalid mesh material link");
          // Keep the actual operator binding. Material shader evaluation is separate.
          for (auto& f : mesh->faces) if (!op.parameters[0] || (f.mask & op.parameters[0])) {
            f.material = *node.links[0]; f.pass = std::bit_cast<std::int32_t>(op.parameters[1]);
          }
        } else if (op.command == 0xa3) {
          require(op,0);
          for (auto& f : mesh->faces) {
            const auto old=f.crease;
            std::reverse(f.vertices.begin(),f.vertices.begin()+f.count);
            for (unsigned c=0;c<f.count;++c) f.crease[c]=old[(2*f.count-2-c)%f.count];
          }
        } else if (op.command == 0xac) {
          require(op,1); smooth_mesh_angle(*mesh,number(op,0));
        } else if (op.command == 0xab) {
          bend(*mesh,op);
        } else if (op.command == 0xa5) uv_project(*mesh,op);
        else if (op.command == 0xa6) {
          require(op,2);
          if (op.parameters[0] != 0 || (op.parameters[1] & ~135u)) throw std::runtime_error("masked Center is not ported");
          const auto bounds = mesh_bounds(*mesh);
          for (auto& v : mesh->vertices) for (unsigned c = 0; c < 3; ++c) if (op.parameters[1] & (1u<<c))
            v.position[c] -= c == 1 && (op.parameters[1]&128) ? bounds.min[c] : (bounds.min[c]+bounds.max[c])*0.5f;
        } else *mesh = repeat(*mesh,op);
      } else throw std::runtime_error("mesh operator " + std::to_string(id) + " (" + std::string(operator_info(op.command).name) + ") is not ported");
      validate_mesh(*mesh);
      const auto bytes = mesh->vertices.capacity()*sizeof(MeshVertex) + mesh->faces.capacity()*sizeof(MeshFace);
      if (bytes > 256*1024*1024-allocated_) throw std::runtime_error("mesh cache exceeds 256 MiB limit");
      allocated_ += bytes; cache_[id] = std::move(mesh);
    }
    visiting_[id] = false;
    return cache_[id];
  }
private:
  const Project& project_; const Graph& graph_;
  std::vector<std::shared_ptr<const Mesh>> cache_; std::vector<bool> visiting_;
  std::size_t allocated_{};
  bool is_material(std::size_t id) const {
    for (unsigned depth = 0; depth < 512; ++depth) {
      if (id >= project_.operators.size() || !graph_.nodes.at(id).active) return false;
      const auto command = project_.operators[id].command;
      const auto& node = graph_.nodes[id];
      if (command == 1) {
        if (node.links.size() != 1 || !node.links[0]) return false;
        id = *node.links[0];
      } else if (command == 2 || command == 3) {
        if (node.inputs.size() != 1) return false;
        id = node.inputs[0];
      } else return operator_info(command).category == "MATERIAL";
    }
    return false;
  }
  static void uv_project(Mesh& mesh, const Operator& op) {
    require(op,11);
    const auto mask = op.parameters[0], type = op.parameters[10];
    if (mask > 255 || type > 3) throw std::runtime_error("invalid UVProject parameters");
    const auto matrix = srt(op,1);
    if (type == 1 || type == 2) {
      // The original cylindrical/spherical branch ignores the selection mask.
      for (auto& v : mesh.vertices) {
        const auto p = transform(matrix,v.position);
        v.uv[0] = 0.5f - std::atan2(double(p[0]),double(p[2]))/tau;
        v.uv[1] = type == 1 ? -p[1] : 0.5f - std::atan2(double(p[1]),std::sqrt(double(p[0]*p[0]+p[2]*p[2])))/std::numbers::pi_v<float>;
      }
    } else {
      // Separate polygon corners for projection seams, retaining face masks.
      Mesh out; reserve(out,mesh.faces.size()*4,mesh.faces.size());
      std::unordered_map<std::uint32_t,std::uint32_t> points;
      for (const auto& f : mesh.faces) {
        auto copy = f;
        const auto n = normal(mesh,f);
        unsigned axis = 0;
        if (std::abs(n[1]) > std::abs(n[axis])) axis = 1;
        if (std::abs(n[2]) > std::abs(n[axis])) axis = 2;
        for (unsigned k = 0; k < f.count; ++k) {
          auto v = mesh.vertices[f.vertices[k]];
          if (!mask || (f.mask & mask)) v.uv = project_uv(transform(matrix,v.position),axis,type == 3 || n[axis] > 0 ? 1 : -1);
          const auto point=points.try_emplace(mesh_point(mesh,f.vertices[k]),out.vertices.size()).first->second;
          copy.vertices[k] = vertex(out,v.position,v.uv,v.mask,point);
        }
        out.faces.push_back(copy);
      }
      mesh = std::move(out);
      crease_mesh_seams(mesh,0);
    }
  }
  static Mesh repeat(const Mesh& source, const Operator& op) {
    require(op,17);
    const auto count = op.parameters[9], mode = op.parameters[10];
    if (!count || count > 4096 || mode > 1)
      throw std::runtime_error("Multiply random mode or count is not supported");
    const auto extrude=number(op,16);
    Mesh wedges;
    std::vector<Vec3> directions;
    if (extrude) {
      const auto normals=mesh_normals(source);
      reserve(wedges,source.faces.size()*4,source.faces.size());
      std::unordered_map<std::uint32_t,std::uint32_t> points;
      for (std::size_t f=0;f<source.faces.size();++f) {
        auto copy=source.faces[f];
        for (unsigned c=0;c<copy.count;++c) {
          const auto& v=source.vertices[copy.vertices[c]];
          const auto point=points.try_emplace(mesh_point(source,copy.vertices[c]),wedges.vertices.size()).first->second;
          copy.vertices[c]=vertex(wedges,v.position,v.uv,v.mask,point);
          directions.push_back(normals[f][c]);
        }
        wedges.faces.push_back(copy);
      }
    }
    const auto& base=extrude ? wedges : source;
    Mesh out; reserve(out,base.vertices.size()*count,base.faces.size()*count);
    auto global = identity, local = identity;
    const auto step = srt(op,0), local_step = euler({number(op,13),number(op,14),number(op,15)});
    for (unsigned i = 0; i < count; ++i) {
      const auto start = out.vertices.size(); append(out,base);
      const auto matrix = multiply(local,global);
      for (std::size_t v = start; v < out.vertices.size(); ++v) {
        if (extrude) for (unsigned c=0;c<3;++c) out.vertices[v].position[c]+=directions[v-start][c]*(i*extrude);
        out.vertices[v].position = transform(matrix,out.vertices[v].position);
        if (mode & 1) { out.vertices[v].uv[0] += i*number(op,11); out.vertices[v].uv[1] += i*number(op,12); }
      }
      global = multiply(global,step); local = multiply(local,local_step);
    }
    return out;
  }
};
}

Bounds mesh_bounds(const Mesh& mesh) {
  if (mesh.vertices.empty()) throw std::runtime_error("empty mesh has no bounds");
  Bounds result{mesh.vertices[0].position,mesh.vertices[0].position};
  for (const auto& v : mesh.vertices) for (unsigned c = 0; c < 3; ++c) {
    result.min[c] = std::min(result.min[c],v.position[c]); result.max[c] = std::max(result.max[c],v.position[c]);
  }
  return result;
}
void validate_mesh(const Mesh& mesh) {
  if (mesh.vertices.size() > element_limit || mesh.faces.size() > element_limit) throw std::runtime_error("mesh resource limit exceeded");
  for (const auto& v : mesh.vertices) for (unsigned c = 0; c < 3; ++c)
    if (!std::isfinite(v.position[c]) || !std::isfinite(v.uv[c])) throw std::runtime_error("non-finite mesh geometry");
  for (const auto& v:mesh.vertices) if (v.point!=mesh_no_point && v.point>=mesh.vertices.size())
    throw std::runtime_error("mesh topological point out of bounds");
  for (const auto& f : mesh.faces) {
    if (f.count < 3 || f.count > 4) throw std::runtime_error("invalid mesh polygon");
    for (unsigned i = 0; i < f.count; ++i) if (f.vertices[i] >= mesh.vertices.size()) throw std::runtime_error("mesh index out of bounds");
  }
}
std::shared_ptr<const Mesh> evaluate_mesh(const Project& project, const Graph& graph, std::size_t id) {
  return Evaluator(project,graph).evaluate(id);
}
void write_obj(const std::filesystem::path& path, const Mesh& mesh) {
  validate_mesh(mesh);
  const auto normals=mesh_normals(mesh);
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot open mesh output");
  out << "# Native Theta geometry preview; material operator IDs are metadata.\n" << std::setprecision(9);
  for (const auto& v : mesh.vertices) out << "v " << v.position[0] << ' ' << v.position[1] << ' ' << v.position[2] << '\n';
  for (const auto& v : mesh.vertices) out << "vt " << v.uv[0] << ' ' << v.uv[1] << ' ' << v.uv[2] << '\n';
  for (std::size_t f=0;f<mesh.faces.size();++f) for (unsigned c=0;c<mesh.faces[f].count;++c) {
    const auto& n=normals[f][c]; out << "vn " << n[0] << ' ' << n[1] << ' ' << n[2] << '\n';
  }
  std::optional<std::size_t> previous; std::int32_t pass = 0; bool first = true;
  std::size_t normal_index=1;
  for (const auto& f : mesh.faces) {
    if (first || previous != f.material || pass != f.pass) {
      out << "g material_" << (f.material ? std::to_string(*f.material) : "default") << "_pass_" << f.pass << '\n';
      first = false; previous = f.material; pass = f.pass;
    }
    out << 'f';
    for (unsigned i = 0; i < f.count; ++i) out << ' ' << f.vertices[i]+1 << '/' << f.vertices[i]+1 << '/' << normal_index++;
    out << '\n';
  }
  if (!out) throw std::runtime_error("cannot write mesh output");
}
} // namespace wz3
