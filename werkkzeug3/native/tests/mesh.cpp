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
  const auto arc=p.get(p.add(0x83,{4,4,f(1),f(0.25f),0,f(0.5f),0}));
  check(arc->vertices.size()==33 && arc->faces.size()==18,"torus arc must keep two unsplit quad caps");
  near(volume(*arc),std::sqrt(0.125f),"half torus winding/volume");
  const auto arc_edges=wz3::mesh_opposites(*arc);
  for (const auto e:arc_edges) check(e!=wz3::mesh_no_point,"torus arc cap disconnected");
  for (unsigned i=0;i<2;++i) for (unsigned c=0;c<4;++c) {
    const auto& cap=arc->faces[16+i];
    near(arc->vertices[cap.vertices[c]].uv[1],float(i),"original torus cap V is constant");
    check(cap.crease[c]==wz3::crease_uv,"torus cap should only crease UV before SmAngle");
  }
  const auto sphere=p.get(p.add(0x84,{4,1}));
  check(sphere->vertices.size()==12 && sphere->faces.size()==12,"sphere ring/cap topology");
  near(volume(*sphere),std::sqrt(2.0f)/8,"sphere cap winding/volume");
  const auto sphere_bounds=wz3::mesh_bounds(*sphere);
  near(sphere_bounds.max[1],std::sqrt(0.125f),"sphere pole must be an averaged cap, not radius 0.5");
  for (std::size_t i=4;i<sphere->faces.size();++i) check(sphere->faces[i].mask==1,"sphere cap face mask");
  for (const auto& v:sphere->vertices) check(v.mask==0,"sphere vertex masks must be cleared");
  const auto opposite=wz3::mesh_opposites(*sphere);
  for (std::size_t i=0;i<sphere->faces.size();++i) for (unsigned c=0;c<sphere->faces[i].count;++c)
    check(opposite[i*4+c]!=wz3::mesh_no_point,"sphere seam is topologically open");
  near(sphere->vertices[10].uv[0],1,"sphere cap center seam U");
  rejects([&]{p.get(p.add(0x81,cube(0)));});
  rejects([&]{p.get(p.add(0x84,{3,1}));});
  rejects([&]{p.get(p.add(0x82,{8,2,0,2}));});
  rejects([&]{p.get(p.add(0x83,{8,8,f(1),f(0.25f),0,f(0.5f),0}));});
}
void bends() {
  Fixture p;
  auto parameters=cube(1,4,1,1); parameters[5]=f(4);
  const auto base=p.add(0x81,parameters); const auto original=p.get(base);
  const auto bent=p.get(p.add(0xab,{0,0,0,0,0,0,f(1),f(0.25f)},{base}));
  for (std::size_t i=0;i<bent->vertices.size();++i) {
    const auto& a=original->vertices[i]; const auto& b=bent->vertices[i];
    if (a.position[1]<=0) for (unsigned c=0;c<3;++c) near(b.position[c],a.position[c],"Bend moved geometry below its start");
    else {
      near(b.position[0],1-a.position[1],"Bend straight tail after length");
      near(b.position[1],a.position[0],"Bend quarter turn");
    }
    near(b.position[2],a.position[2],"Bend changed local Z");
    check(b.uv==a.uv && b.point==a.point && b.mask==a.mask,"Bend changed vertex attributes");
  }
  const auto unit=p.add(0x81,cube()); const auto source=p.get(unit);
  const auto offset=p.get(p.add(0xab,{f(-1),f(-0.5f),0,0,0,0,f(1),f(0.25f)},{unit}));
  for (std::size_t i=0;i<source->vertices.size();++i) if (source->vertices[i].position[1]==0.5f) {
    near(offset->vertices[i].position[0],-1,"Bend center X");
    near(offset->vertices[i].position[1],source->vertices[i].position[0]+0.5f,"Bend center Y");
  }
  const auto rotated=p.get(p.add(0xab,{0,0,0,0,0,f(0.25f),f(1),f(0.25f)},{unit}));
  for (std::size_t i=0;i<source->vertices.size();++i) if (source->vertices[i].position==wz3::Vec3{0.5f,0.5f,0.5f}) {
    near(rotated->vertices[i].position[0],-std::sqrt(0.125f),"Bend rotated local frame X");
    near(rotated->vertices[i].position[1],std::sqrt(0.125f),"Bend rotated local frame Y");
  }
  const auto below=p.get(p.add(0xab,{f(1),f(5),f(3),0,0,f(0.25f),f(1),f(0.25f)},{unit}));
  for (std::size_t i=0;i<source->vertices.size();++i) for (unsigned c=0;c<3;++c)
    near(below->vertices[i].position[c],source->vertices[i].position[c],"Bend inverse rigid transform");
  rejects([&]{p.get(p.add(0xab,{0,0,0,0,0,0,0,f(0.25f)},{unit}));});
  wz3::mesh_normals(*bent);
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

  // SmAngle introduces hard corners inside shared cylinder wedges. Extruding
  // copies must split those normal wedges, then apply SRT, preserving UV offsets.
  const auto faceted=p.add(0xac,{0},{p.add(0x82,{8,1,0,1})});
  mesh=p.get(faceted); n=wz3::mesh_normals(*mesh);
  const auto layers=p.get(p.add(0x95,{f(2),f(2),f(2),0,0,0,f(3),0,0,2,1,f(0.25f),f(0.5f),0,0,0,f(0.1f)},{faceted}));
  for (std::size_t i=0;i<mesh->faces.size();++i) for (unsigned c=0;c<mesh->faces[i].count;++c) {
    const auto& a=mesh->vertices[mesh->faces[i].vertices[c]];
    const auto& b=layers->vertices[layers->faces[i+mesh->faces.size()].vertices[c]];
    for (unsigned k=0;k<3;++k) near(b.position[k],(a.position[k]+n[i][c][k]*0.1f)*2+(k==0 ? 3 : 0),"normal extrusion must precede accumulated SRT");
    near(b.uv[0],a.uv[0]+0.25f,"extruded Multiply U offset");
    near(b.uv[1],a.uv[1]+0.5f,"extruded Multiply V offset");
  }
  wz3::mesh_normals(*layers);
}
}
int main(int argc,char** argv) {
  try {
    check(argc==2,"expected Theta project");primitives();operators();normals();bends();
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);
    for (const auto* name : {"mesh.cube","mesh.bwflattorus","mesh.LineCircle","mesh.FireGradientCircle",
        "mesh.bwflattorus8","mesh.bwflattorus4","mesh.flatfractal","mesh.g4flat","mesh.citybridge",
        "mesh.cityblock","mesh.rohrcylin120","mesh.rohrcylin32","msh.wfl1","mesh.tetra","mesh.tetra2","mesh.Arrow",
        "mesh.fireball","mesh.fireball2","mesh.highway1","mesh.highway2"}) {
      const auto mesh=wz3::evaluate_mesh(project,graph,graph.names.at(name));
      wz3::validate_mesh(*mesh);check(!mesh->faces.empty(),"empty Theta mesh");
      wz3::mesh_normals(*mesh);
    }
    rejects([&]{wz3::evaluate_mesh(project,graph,graph.names.at("mesh.stars"));});
    std::cout << "mesh geometry, topology, weighted normals and 20 real Theta recipes passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
