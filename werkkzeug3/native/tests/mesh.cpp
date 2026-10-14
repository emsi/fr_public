// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/mesh.hpp"
#include "../src/mesh_topology.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float a, float b, const char* message) { check(std::abs(a-b) < 0.0001f,message); }
std::uint32_t f(float value) { return std::bit_cast<std::uint32_t>(value); }
template<class F> void rejects(F action) {
  bool rejected = false;
  try { action(); } catch (const std::runtime_error&) { rejected = true; }
  check(rejected,"unsupported/invalid mesh request accepted");
}
struct Fixture {
  wz3::Project project; wz3::Graph graph;
  std::size_t add(unsigned command, std::vector<std::uint32_t> parameters, std::vector<std::size_t> inputs = {},
                  std::vector<std::optional<std::size_t>> links = {}) {
    wz3::Operator op; op.command = command; op.parameters = std::move(parameters);
    project.operators.push_back(std::move(op)); graph.nodes.push_back({true,true,inputs,links});
    return project.operators.size()-1;
  }
  auto get(std::size_t id) { return wz3::evaluate_mesh(project,graph,id); }
};
double volume(const wz3::Mesh& mesh) {
  double result = 0;
  for (const auto& face : mesh.faces) for (unsigned i = 1; i + 1 < face.count; ++i) {
    const auto& a = mesh.vertices[face.vertices[0]].position;
    const auto& b = mesh.vertices[face.vertices[i]].position;
    const auto& c = mesh.vertices[face.vertices[i+1]].position;
    result += (double(a[0])*(b[1]*c[2]-b[2]*c[1]) + double(a[1])*(b[2]*c[0]-b[0]*c[2]) + double(a[2])*(b[0]*c[1]-b[1]*c[0]))/6;
  }
  return result;
}
std::vector<std::uint32_t> cube(unsigned x=1,unsigned y=1,unsigned z=1,unsigned flags=0) {
  return {x,y,z,flags,f(1),f(1),f(1),0,0,0,0,0,0};
}
void primitives() {
  Fixture p;
  const auto a = p.add(0x81,cube());
  const auto mesh = p.get(a);
  check(mesh->vertices.size()==24 && mesh->faces.size()==6,"unit cube topology");
  near(volume(*mesh),1,"cube winding/volume");
  const auto bounds = wz3::mesh_bounds(*mesh);
  for (unsigned c=0;c<3;++c) { near(bounds.min[c],-0.5f,"cube lower bound"); near(bounds.max[c],0.5f,"cube upper bound"); }
  const auto tess = p.get(p.add(0x81,cube(2,3,4)));
  check(tess->faces.size()==52 && tess->vertices.size()==94,"cube tessellation");
  near(volume(*tess),1,"tessellation changed cube volume");
  auto uv_cube = cube(1,1,1,10); uv_cube[4]=f(2);uv_cube[5]=f(3);uv_cube[6]=f(4);
  const auto wrapped = p.get(p.add(0x81,uv_cube));
  float min_u=100,max_u=-100;
  for (auto& v:wrapped->vertices) { min_u=std::min(min_u,v.uv[0]);max_u=std::max(max_u,v.uv[0]); }
  near(min_u,0,"cube wrap UV start");near(max_u,12,"cube wrap UV perimeter");
  const auto cylinder = p.get(p.add(0x82,{8,2,0,1}));
  check(cylinder->vertices.size()==45 && cylinder->faces.size()==32,"cylinder fan caps/tessellation");
  near(volume(*cylinder),std::sqrt(0.5f),"cylinder winding/volume");
  for (unsigned y=0;y<3;++y) check(cylinder->vertices[y*9].position==cylinder->vertices[y*9+8].position,"cylinder seam split moved position");
  const auto open = p.get(p.add(0x82,{8,2,1,1}));
  check(open->faces.size()==16,"open cylinder must omit caps");
  const auto torus = p.get(p.add(0x83,{8,8,f(1),f(0.25f),0,f(1),0}));
  check(torus->vertices.size()==81 && torus->faces.size()==64,"torus tessellation");
  near(volume(*torus),1,"torus winding/volume");
  for (unsigned y=0;y<9;++y) check(torus->vertices[y].position==torus->vertices[72+y].position,"torus major seam");
  rejects([&]{p.get(p.add(0x81,cube(0)));});
  rejects([&]{p.get(p.add(0x82,{8,2,0,2}));});
  rejects([&]{p.get(p.add(0x83,{8,8,f(1),f(0.25f),0,f(0.5f),0}));});
}
void operators() {
  Fixture p;
  const auto base = p.add(0x81,cube());
  const auto transformed = p.get(p.add(0x88,{0,f(2),f(3),f(4),0,0,f(0.25f),f(5),f(6),f(7)},{base}));
  const auto& point = transformed->vertices[0].position;
  near(point[0],6.5,"SRT x");near(point[1],5,"SRT y");near(point[2],5,"SRT z");
  const auto cylinder = p.add(0x82,{8,1,0,1});
  const auto select = p.add(0x86,{0x10100,2,0,f(0.5),0,f(2),f(0.01),f(2)},{cylinder});
  const auto stretched = p.get(p.add(0x88,{1,f(1),f(1),f(1),0,0,0,0,f(2),0},{select}));
  auto box=wz3::mesh_bounds(*stretched);
  near(box.min[1],-0.5,"selected transform moved lower cap");near(box.max[1],2.5,"selected transform missed upper cap");
  const auto uv = p.get(p.add(0x89,{0,f(1),f(1),f(1),0,0,0,f(0.5),f(0.5),0,0,5},{base}));
  near(uv->vertices[0].uv[0],0,"position-to-UV transform");
  const auto repeats = p.get(p.add(0x95,{f(1),f(1),f(1),0,0,0,f(2),0,0,3,0,0,0,0,0,0,0},{base}));
  check(repeats->faces.size()==18,"Multiply copy count");
  box=wz3::mesh_bounds(*repeats);near(box.min[0],-0.5,"Multiply starts with original");near(box.max[0],4.5,"Multiply accumulated translation");
  const auto added = p.get(p.add(0x92,{}, {base,base}));
  check(added->vertices.size()==48 && added->faces[6].vertices[0]==24,"Add index offset");
  const auto inverted = p.get(p.add(0xa3,{}, {base}));near(volume(*inverted),-1,"Invert winding");
  const auto material = p.add(0xd0,{});
  const auto bound = p.get(p.add(0x96,{0,3},{base},{material}));
  for (const auto& face:bound->faces) check(face.material==material && face.pass==3,"material binding/pass metadata");
  rejects([&]{p.get(p.add(0x96,{0,0},{base},{base}));});
  const auto cycle = p.add(3,{});p.graph.nodes[cycle].inputs={cycle};
  rejects([&]{p.get(cycle);});
  rejects([&]{p.get(p.add(0x87,{0,f(1),1},{base}));});
  const auto animated = p.add(0x88,{0,f(1),f(1),f(1),0,0,0,0,0,0},{base});
  p.project.operators[animated].animations.push_back({});rejects([&]{p.get(animated);});
}
void normals() {
  // Two unequal triangles: area weighting would favor Y by 4:1. GenMesh's
  // inverse squared-edge-product weighting instead favors Z by 4:1.
  wz3::Mesh weighted;
  weighted.vertices={{{0,0,0},{0,0,0},0},{{2,0,0},{1,0,0},0},
                     {{0,1,0},{0,1,0},0},{{0,0,4},{0,1,0},0},{{0,0,0},{1,1,0},0,0}};
  weighted.faces={{{0,1,2,0},3,0,{},0},{{1,4,3,0},3,0,{},0}};
  auto n=wz3::mesh_normals(weighted);
  near(n[0][0][1],1/std::sqrt(17.0f),"GenMesh inverse edge weight Y");
  near(n[0][0][2],4/std::sqrt(17.0f),"GenMesh inverse edge weight Z");
  check(n[0][0]==n[1][1],"UV seam broke smoothing");
  wz3::smooth_mesh_angle(weighted,0.5f);
  n=wz3::mesh_normals(weighted);
  near(n[0][0][2],1,"SmAngle did not crease sharp corner");
  near(n[1][1][1],1,"SmAngle reverse edge crease");
  wz3::smooth_mesh_angle(weighted,1.0f);
  check(wz3::mesh_normals(weighted)==n,"SmAngle removed existing crease");
  weighted.faces.push_back(weighted.faces[0]);
  rejects([&]{wz3::mesh_normals(weighted);});

  Fixture p;
  const auto soft=p.add(0x81,cube());
  auto mesh=p.get(soft); n=wz3::mesh_normals(*mesh);
  for (std::size_t i=0;i<mesh->faces.size();++i) for (unsigned c=0;c<4;++c)
    for (unsigned k=0;k<3;++k)
      near(n[i][c][k],mesh->vertices[mesh->faces[i].vertices[c]].position[k]*2/std::sqrt(3.0f),"smooth cube corner");
  const auto hard=p.add(0x81,cube(2,3,4,1));
  mesh=p.get(hard); n=wz3::mesh_normals(*mesh);
  for (std::size_t i=0;i<mesh->faces.size();++i) for (unsigned c=0;c<4;++c) {
    unsigned side=0; while (!(mesh->faces[i].mask&(1u<<side))) ++side;
    for (unsigned k=0;k<3;++k) near(n[i][c][k],k==side/2 ? (side&1 ? 1 : -1) : 0,"hard cube face normal");
  }
  const auto inverted=p.get(p.add(0xa3,{}, {hard}));
  const auto inverted_normals=wz3::mesh_normals(*inverted);
  for (std::size_t i=0;i<n.size();++i) for (unsigned c=0;c<4;++c) for (unsigned k=0;k<3;++k)
    near(inverted_normals[i][3-c][k],-n[i][c][k],"Invert must reverse crease edges and normals");
  const auto projected=p.get(p.add(0xa5,{0,f(1),f(1),f(1),0,0,0,0,0,0,0},{hard}));
  check(wz3::mesh_normals(*projected)==n,"UV projection lost topology/creases");
  const auto doubled=p.get(p.add(0x92,{}, {hard,hard}));
  const auto both=wz3::mesh_normals(*doubled);
  for (std::size_t i=0;i<n.size();++i) check(both[i]==n[i] && both[i+n.size()]==n[i],"Add welded coincident components");
  const auto cylinder=p.get(p.add(0x82,{8,2,0,1})); n=wz3::mesh_normals(*cylinder);
  for (std::size_t i=0;i<cylinder->faces.size();++i) for (unsigned c=0;c<cylinder->faces[i].count;++c) {
    const auto& v=cylinder->vertices[cylinder->faces[i].vertices[c]];
    if (cylinder->faces[i].mask) near(n[i][c][1],v.position[1]*2,"cap must stay flat");
    else { near(n[i][c][1],0,"cap normal leaked into cylinder side");
           near(n[i][c][0],v.position[0]*2,"cylinder side smooth X");
           near(n[i][c][2],v.position[2]*2,"cylinder side smooth Z"); }
  }
}
}
int main(int argc,char** argv) {
  try {
    check(argc==2,"expected Theta project");primitives();operators();normals();
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);
    for (const auto* name : {"mesh.cube","mesh.bwflattorus","mesh.LineCircle","mesh.FireGradientCircle",
        "mesh.bwflattorus8","mesh.bwflattorus4","mesh.flatfractal","mesh.g4flat","mesh.citybridge",
        "mesh.cityblock","mesh.rohrcylin120","mesh.rohrcylin32","msh.wfl1","mesh.tetra","mesh.tetra2","mesh.Arrow"}) {
      const auto mesh=wz3::evaluate_mesh(project,graph,graph.names.at(name));
      wz3::validate_mesh(*mesh);check(!mesh->faces.empty(),"empty Theta mesh");
      wz3::mesh_normals(*mesh);
    }
    rejects([&]{wz3::evaluate_mesh(project,graph,graph.names.at("mesh.stars"));});
    std::cout << "mesh geometry, topology, weighted normals and 16 real Theta recipes passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
