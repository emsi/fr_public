// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/mesh.hpp"
#include "wz3/texture.hpp"
#include "../src/mesh_topology.hpp"
#include "../src/mesh_kernel.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
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
void seams(const wz3::Mesh& mesh) {
  unsigned wedges=0;
  for(const auto& v:mesh.vertices) {
    check(v.point<mesh.vertices.size(),"missing primitive point identity");
    for(unsigned c=0;c<3;++c)near(v.position[c],mesh.vertices[v.point].position[c],"primitive seam moved position");
    wedges+=v.uv!=mesh.vertices[v.point].uv;
  }
  check(wedges>0,"primitive lost UV wedges");
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
  seams(*cylinder);
  const auto open = p.get(p.add(0x82,{8,2,1,1}));
  check(open->faces.size()==16,"open cylinder must omit caps");
  const auto torus = p.get(p.add(0x83,{8,8,f(1),f(0.25f),0,f(1),0}));
  check(torus->vertices.size()==81 && torus->faces.size()==64,"torus tessellation");
  near(volume(*torus),1,"torus winding/volume");
  seams(*torus);
  const auto arc=p.get(p.add(0x83,{4,4,f(1),f(0.25f),0,f(0.5f),0}));
  check(arc->vertices.size()==33 && arc->faces.size()==18,"torus arc must keep two unsplit quad caps");
  near(volume(*arc),std::sqrt(0.125f),"half torus winding/volume");
  const auto arc_edges=wz3::mesh_opposites(*arc);
  for (const auto e:arc_edges) check(e!=wz3::mesh_no_point,"torus arc cap disconnected");
  for (unsigned i=0;i<2;++i) for (unsigned c=0;c<4;++c) {
    const auto& cap=arc->faces[i];
    near(arc->vertices[cap.vertices[c]].uv[1],float(1-i),"original torus cap V is constant");
    check(cap.crease[c]==wz3::crease_uv,"torus cap should only crease UV before SmAngle");
  }
  const auto sphere=p.get(p.add(0x84,{4,1}));
  check(sphere->vertices.size()==12 && sphere->faces.size()==12,"sphere ring/cap topology");
  near(volume(*sphere),std::sqrt(2.0f)/8,"sphere cap winding/volume");
  const auto sphere_bounds=wz3::mesh_bounds(*sphere);
  near(sphere_bounds.max[1],std::sqrt(0.125f),"sphere pole must be an averaged cap, not radius 0.5");
  unsigned caps=0;
  for (const auto& face:sphere->faces) if(face.count==3) {check(face.mask==1,"sphere cap face mask");++caps;}
  check(caps==8,"sphere cap triangles");
  for (const auto& v:sphere->vertices) check(v.mask==0,"sphere vertex masks must be cleared");
  const auto opposite=wz3::mesh_opposites(*sphere);
  for (std::size_t i=0;i<sphere->faces.size();++i) for (unsigned c=0;c<sphere->faces[i].count;++c)
    check(opposite[i*4+c]!=wz3::mesh_no_point,"sphere seam is topologically open");
  for(const auto& v:sphere->vertices) if(std::abs(v.position[0])<1e-5f && std::abs(v.position[2])<1e-5f)
    near(v.uv[0],1,"sphere cap center seam U");
  rejects([&]{p.get(p.add(0x81,cube(0)));});
  rejects([&]{p.get(p.add(0x84,{3,1}));});
  near(volume(*p.get(p.add(0x82,{8,2,0,2}))),std::sqrt(0.5f),"cylinder radial subdivisions");
  near(volume(*p.get(p.add(0x83,{8,8,f(1),f(0.25f),0,f(0.5f),0}))),8*std::sin(std::numbers::pi_v<float>/8)*std::sqrt(0.03125f),"polygonal open torus caps");
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
  unsigned offset_checked=0,rotation_checked=0;
  for (std::size_t i=0;i<source->vertices.size();++i) if (std::abs(source->vertices[i].position[1]-0.5f)<1e-5f) {
    near(offset->vertices[i].position[0],-1,"Bend center X");
    near(offset->vertices[i].position[1],source->vertices[i].position[0]+0.5f,"Bend center Y");
    ++offset_checked;
  }
  const auto rotated=p.get(p.add(0xab,{0,0,0,0,0,f(0.25f),f(1),f(0.25f)},{unit}));
  for (std::size_t i=0;i<source->vertices.size();++i) if (std::ranges::all_of(source->vertices[i].position,[](float v){return std::abs(v-.5f)<1e-5f;})) {
    near(rotated->vertices[i].position[0],-std::sqrt(0.125f),"Bend rotated local frame X");
    near(rotated->vertices[i].position[1],std::sqrt(0.125f),"Bend rotated local frame Y");
    ++rotation_checked;
  }
  check(offset_checked>0 && rotation_checked>0,"Bend fixtures did not exercise center/rotation");
  const auto below=p.get(p.add(0xab,{f(1),f(5),f(3),0,0,f(0.25f),f(1),f(0.25f)},{unit}));
  for (std::size_t i=0;i<source->vertices.size();++i) for (unsigned c=0;c<3;++c)
    near(below->vertices[i].position[c],source->vertices[i].position[c],"Bend inverse rigid transform");
  rejects([&]{p.get(p.add(0xab,{0,0,0,0,0,0,0,f(0.25f)},{unit}));});
  wz3::mesh_normals(*bent);
}
void operators() {
  Fixture p;
  const auto base = p.add(0x81,cube());
  const auto original=p.get(base);
  const auto transformed = p.get(p.add(0x88,{0,f(2),f(3),f(4),0,0,f(0.25f),f(5),f(6),f(7)},{base}));
  for(std::size_t i=0;i<original->vertices.size();++i) {
    const auto& a=original->vertices[i].position;const auto& b=transformed->vertices[i].position;
    near(b[0],5-3*a[1],"SRT x");near(b[1],6+2*a[0],"SRT y");near(b[2],7+4*a[2],"SRT z");
  }
  const auto cylinder = p.add(0x82,{8,1,0,1});
  const auto select = p.add(0x86,{0x10100,2,0,f(0.5),0,f(2),f(0.01),f(2)},{cylinder});
  const auto stretched = p.get(p.add(0x88,{1,f(1),f(1),f(1),0,0,0,0,f(2),0},{select}));
  auto box=wz3::mesh_bounds(*stretched);
  near(box.min[1],-0.5,"selected transform moved lower cap");near(box.max[1],2.5,"selected transform missed upper cap");
  const auto uv = p.get(p.add(0x89,{0,f(1),f(1),f(1),0,0,0,f(0.5),f(0.5),0,0,5},{base}));
  near(uv->vertices[0].uv[0],original->vertices[0].position[0]+0.5f,"position-to-UV transform");
  const auto repeats = p.get(p.add(0x95,{f(1),f(1),f(1),0,0,0,f(2),0,0,3,0,0,0,0,0,0,0},{base}));
  check(repeats->faces.size()==18,"Multiply copy count");
  box=wz3::mesh_bounds(*repeats);near(box.min[0],-0.5,"Multiply starts with original");near(box.max[0],4.5,"Multiply accumulated translation");
  const auto added = p.get(p.add(0x92,{}, {base,base}));
  check(added->vertices.size()==48 && added->faces[6].vertices[0]==original->faces[0].vertices[0]+24,"Add index offset");
  const auto inverted = p.get(p.add(0xa3,{}, {base}));near(volume(*inverted),-1,"Invert winding");
  const auto material = p.add(0xd0,{});
  const auto bound = p.get(p.add(0x96,{0,3},{base},{material}));
  for (const auto& face:bound->faces) check(face.material==material && face.pass==3,"material binding/pass metadata");
  rejects([&]{p.get(p.add(0x96,{0,0},{base},{base}));});
  const auto cycle = p.add(3,{});p.graph.nodes[cycle].inputs={cycle};
  rejects([&]{p.get(cycle);});
  rejects([&]{p.get(p.add(0x87,{0,f(1),99},{base}));});
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
    near(inverted_normals[i][c][k],-n[i][0][k],"Invert must reverse crease edges and normals");
  const auto projected=p.get(p.add(0xa5,{0,f(1),f(1),f(1),0,0,0,0,0,0,0},{hard}));
  const auto projected_normals=wz3::mesh_normals(*projected);
  for(std::size_t i=0;i<n.size();++i) for(unsigned c=0;c<4;++c) for(unsigned k=0;k<3;++k)
    near(projected_normals[i][c][k],n[i][c][k],"UV projection lost topology/creases");
  const auto doubled=p.get(p.add(0x92,{}, {hard,hard}));
  const auto both=wz3::mesh_normals(*doubled);
  for (std::size_t i=0;i<n.size();++i) for(unsigned c=0;c<4;++c) for(unsigned k=0;k<3;++k) {
    near(both[i][c][k],n[i][c][k],"Add changed first component normals");
    near(both[i+n.size()][c][k],n[i][c][k],"Add welded coincident components");
  }
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
void indexed_operations() {
  Fixture p;const auto base=p.add(0x81,cube());
  const auto box=p.get(base);
  const unsigned expected_masks[]={32,16,2,8,1,4};
  for(unsigned i=0;i<6;++i)check(box->faces[i].mask==expected_masks[i],"original cube face numbering");
  const auto selected=p.add(0xa9,{64,2,3},{base});
  const auto extruded=p.get(p.add(0x9a,{64,0,4,2,0,f(1),0,f(1),f(1),f(1),0,0,0},{selected}));
  check(extruded->kernel->Face.Count==14,"extrusion must keep cap and add four side faces per step");
  near(volume(*extruded),2,"two-step cube extrusion volume");
  const auto bounds=wz3::mesh_bounds(*extruded);
  near(bounds.min[1],-0.5f,"extrusion moved unselected bottom");near(bounds.max[1],1.5f,"face 3 must extrude positive Y");
  const auto first=p.add(0xa9,{64,2,2},{base});
  const auto adjacent=p.add(0xa9,{64,0,3},{first});
  const auto region=p.get(p.add(0x9a,{64,0,2,1,f(.1f),f(.1f),f(.1f),f(1),f(1),f(1),0,0,0},{adjacent}));
  const auto individual=p.get(p.add(0x9a,{64,0,3,1,f(.1f),f(.1f),f(.1f),f(1),f(1),f(1),0,0,0},{adjacent}));
  check(region->kernel->Face.Count==12 && individual->kernel->Face.Count==14,"region versus individual face extrusion");
  const auto subdivided=p.get(p.add(0x87,{0,f(1),1},{base}));
  check(subdivided->kernel->Face.Count==24,"subdivision must split all six quads into four");
  unsigned rounded=0;
  for(const auto& face:subdivided->faces)for(unsigned c=0;c<face.count;++c) {
    const auto& v=subdivided->vertices[face.vertices[c]].position;
    if(std::abs(std::abs(v[0])-5.0f/18)<1e-5f && std::abs(std::abs(v[1])-5.0f/18)<1e-5f && std::abs(std::abs(v[2])-5.0f/18)<1e-5f)++rounded;
  }
  check(rounded==24,"subdivision corner weights");
  const auto linear=p.get(p.add(0x87,{0,0,2},{base}));
  near(volume(*linear),1,"alpha zero subdivision must preserve cube volume");
  const auto partial=p.get(p.add(0x87,{64,0,1},{selected}));
  check(partial->kernel->Face.Count==9,"selected face subdivision");near(volume(*partial),1,"partial subdivision boundary");
  const auto smoothed=p.get(p.add(0x87,{0,f(1),3},{base}));
  check(wz3::mesh_bounds(*smoothed).max[0]<.49f,"obsolete subdivision vertices must not expand bounds");

  const auto cylinder=p.add(0x82,{8,2,0,1});
  // Independently calculated from the archived x86 PRNG: skip 56 edge draws
  // and 45 vertex draws before the 32 face draws, preserving the <= comparison.
  const std::uint32_t masks[]={0x64def852,0xd0a31e85,0x64def852};
  const std::uint32_t seeds[]={0,3,0x80000000u};
  for(unsigned j=0;j<3;++j) {
    const auto random=p.get(p.add(0x94,{0x404000,2,128,seeds[j]},{cylinder}));
    std::uint32_t mask=0;
    for(unsigned i=0;i<random->faces.size();++i)if(random->faces[i].mask&64)mask|=1u<<i;
    check(mask==masks[j],"seeded selection must preserve indexed draw order");
    for(const auto& v:random->vertices)check(bool(v.mask&64)==bool(random->vertices[v.point].mask&64),"random selection split a vertex cycle");
  }
  rejects([&]{p.get(p.add(0x9a,{0,0,4,0,0,f(1),0,f(1),f(1),f(1),0,0,0},{base}));});
}
void attributes_and_grids() {
  Fixture p;
  const auto grid=p.get(p.add(0x9d,{3,3,3}));
  check(grid->faces.size()==18,"two-sided grid tessellation");
  auto n=wz3::mesh_normals(*grid);
  unsigned interior=0;
  for(std::size_t i=0;i<grid->faces.size();++i) {
    const auto& face=grid->faces[i];wz3::Vec3 center{};
    for(unsigned c=0;c<4;++c)for(unsigned k=0;k<3;++k)center[k]+=grid->vertices[face.vertices[c]].position[k]*.25f;
    const auto side=face.mask&3;
    check(side==1 || side==2,"grid side masks");
    const bool outer_x=std::abs(center[0])>.2f,outer_z=std::abs(center[2])>.2f;
    check(face.mask==(side|(outer_z ? 4 : 8)|(outer_x ? 16 : 32)|((outer_x||outer_z) ? 64 : 128)),"grid boundary masks");
    interior+=!outer_x && !outer_z;
    for(unsigned c=0;c<4;++c) {near(n[i][c][1],side==1 ? -1 : 1,"grid normal side");near(center[1],0,"grid XZ plane");}
  }
  check(interior==2,"grid interior faces");
  const auto single=p.get(p.add(0x9d,{0,3,3}));
  check(single->faces.size()==9,"single-sided grid face deletion");
  for(const auto& face:single->faces)check((face.mask&3)==1,"single-sided grid must keep negative Y side");
  const auto base=p.add(0x81,cube(1,1,1,1));
  const auto ambient=p.get(p.add(0xad,{0,0,0,0,0,0xff804020,f(2),f(8),3},{base}));
  for(const auto& v:ambient->vertices) {
    near(v.color[0],256.0f/255,"HDR ambient red");near(v.color[1],128.0f/255,"ambient green");
    near(v.color[2],64.0f/255,"ambient blue");near(v.color[3],0,"lighting must preserve stored alpha");
  }
  const auto directional=p.get(p.add(0xad,{0,0,0,0,0,0xffffffff,f(1),f(8),0},{base}));
  for(const auto& v:directional->vertices)near(v.color[0],v.mask&8 ? 1 : 0,"directional vertex lighting");
  const auto point=p.get(p.add(0xad,{0,f(2),0,0,0,0xffffffff,f(1),f(10),1},{base}));
  for(const auto& v:point->vertices)near(v.color[0],v.mask&8 ? 1.5f*(1/std::sqrt(2.75f)-.1f) : 0,"point-light falloff");
  const auto shadows=p.get(p.add(0xb0,{0},{base}));
  for(const auto& face:shadows->faces)check(!face.casts_shadow,"ShadowEnable metadata");
  const auto normal=wz3::mesh_normals(*ambient);
  for(std::size_t f=0;f<ambient->faces.size();++f)for(unsigned c=0;c<4;++c) {
    const auto& t=ambient->vertices[ambient->faces[f].vertices[c]].tangent;
    float dot=0,len=0;for(unsigned k=0;k<3;++k) {dot+=normal[f][c][k]*t[k];len+=t[k]*t[k];}
    near(dot,0,"tangent must be perpendicular to normal");near(len,1,"unit tangent");
  }
  const auto logic=p.get(p.add(0xa1,{0x010100,0x010100,0x404000,19},{base}));
  for(const auto& face:logic->faces)check(bool(face.mask&64)==!(face.mask&1),"Select Logic inversion");
  const auto triangulated=p.get(p.add(0x8c,{0,4,0},{base}));
  check(triangulated->faces.size()==24,"fan triangulation of six cube quads");near(volume(*triangulated),1,"triangulation volume");

  const auto bend_base=p.add(0x81,cube(1,4,1));const auto original=p.get(bend_base);
  for(unsigned mode:{0u,1u,4u,5u}) {
    const auto bent=p.get(p.add(0xa0,{0,f(1),f(1),f(1),0,0,0,0,0,0,f(1),f(1),f(1),0,0,0,f(2),0,0,0,0,f(-.5f),f(.5f),mode},{bend_base}));
    for(std::size_t i=0;i<bent->vertices.size();++i) {
      float t=std::clamp(original->vertices[i].position[1]+.5f,0.0f,1.0f);
      if(mode==1)t=t*t*(3-2*t);
      else if(mode==4)t=1-std::abs(t-.5f)*2;
      else if(mode==5)t=t*t*(16+t*(16*t-32));
      near(bent->vertices[i].position[0],original->vertices[i].position[0]+2*t,"Bend interpolation mode");
    }
  }
}
void displacement() {
  Fixture p;const auto base=p.add(0x81,cube(1,1,1,1));
  const auto original=p.get(base);
  // Blue alone controls height. At a hard cube corner, three normal wedges
  // contribute one third each; all three copies must move together.
  for(auto color:{0xff0000ffu,0xffffffffu,0xff00ff00u}) {
    const auto bitmap=p.add(0x21,{2,2,color});
    const auto mesh=p.get(p.add(0x8f,{0,f(3),f(3),f(3)},{base,bitmap}));
    const float height=((color&255) ? 16383.0f : -16384.0f)/32767;
    for(std::size_t i=0;i<mesh->vertices.size();++i)for(unsigned c=0;c<3;++c)
      near(mesh->vertices[i].position[c],original->vertices[i].position[c]*(1+2*height),"blue displacement and corner averaging");
    seams(*mesh);
  }
  const auto bitmap=p.add(0x21,{2,2,0xff0000ff});
  const auto selected=p.add(0x86,{0x400000,2,0,f(.5f),0,f(2),f(.01f),f(2)},{base});
  const auto partial=p.get(p.add(0x8f,{64,f(3),0,0},{selected,bitmap}));
  for(std::size_t i=0;i<partial->vertices.size();++i) {
    const auto& a=original->vertices[i].position;const auto& b=partial->vertices[i].position;
    near(b[0],a[0]*(a[1]>0 ? 1+2*(16383.0f/32767) : 1),"masked displacement");
    near(b[1],a[1],"zero Y displacement amplitude");near(b[2],a[2],"zero Z displacement amplitude");
  }
  seams(*partial);
  rejects([&]{p.get(p.add(0x8f,{0,f(1),f(1),f(1)},{base}));});

  wz3::Texture texture;texture.width=4;texture.height=2;
  for(unsigned i=0;i<8;++i)texture.pixels.push_back({32767,32767,std::uint16_t(i*1024),32767});
  for(auto [u,x]:{std::pair{8192.25f,1},std::pair{-8192.25f,3},std::pair{65536.25f,1},std::pair{-65536.25f,3}}) {
    wz3::legacy::GenMesh mesh;mesh.Init(wz3::legacy::sGMF_DEFAULT,1);const auto v=mesh.AddNewVert();
    mesh.VertNorm(v).Init(1,0,0);mesh.buffer[v*5+4].Init(u,16384.5f,0);mesh.All2Sel();
    mesh.Displace(texture,1,1,1);
    near(mesh.VertPos(v).x,((x+4)*1024.0f-16384)/32767,"64-bit conversion must wrap large positive and negative UVs");
    check(!mesh.Vert[v].Select,"displacement must consume vertex selection");
    mesh.buffer[v*5+4].x=std::numeric_limits<float>::infinity();mesh.All2Sel();
    rejects([&]{mesh.Displace(texture,1,1,1);});
  }
}
void noise_displacement() {
  // Goldens calculated with exact rational arithmetic, rounding each scalar
  // operation down to binary32, using the archived seed-one permutation.
  // These are vector lattice noise, not scalar dot-product Perlin noise.
  struct Sample {wz3::Vec3 p;std::array<std::uint32_t,3> bits;};
  const Sample samples[]={
    {{0,0,0},{0x3f800000,0xbf800000,0}},
    {{-1,0,0},{0,0,0xbf800000}},
    {{1,2,3},{0,0x40400000,0x40400000}},
    {{.25f,.5f,.75f},{0x3f941f20,0x3f02be40,0x3f400000}},
    {{-.25f,-.5f,-.75f},{0xbf007e40,0xbf0a81c0,0xbf941f20}},
    {{.1f,-1.2f,3.4f},{0x3f398ae7,0xc00969bc,0x40444fb9}},
    {{12.345f,-.123f,6.789f},{0x41397693,0x3ee28596,0x40e061d7}}};
  std::fenv_t previous;std::fegetenv(&previous);
  for(auto rounding:{FE_TONEAREST,FE_UPWARD})for(const auto& sample:samples) {
    wz3::legacy::GenMesh mesh;mesh.Init(wz3::legacy::sGMF_DEFAULT,2);
    const auto v=mesh.AddNewVert();mesh.VertPos(v).Init(sample.p[0],sample.p[1],sample.p[2]);mesh.Vert[v].Select=1;
    const auto untouched=mesh.AddNewVert();mesh.VertPos(untouched).Init(7,8,9);
    wz3::legacy::sMatrix matrix;matrix.Init();
    std::fesetround(rounding);std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
    mesh.Perlin(matrix,{1,1,1,0});
    check(std::fegetround()==rounding,"Perlin must restore caller rounding mode");
    check(std::fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO,"Perlin must restore caller exception flags");
    for(unsigned c=0;c<3;++c) {
      const auto value=mesh.VertPos(v)[c];
      check((value==0 ? 0 : f(value))==sample.bits[c],"Perlin downward binary32 golden");
      check(mesh.VertPos(untouched)[c]==float(7+c),"Perlin changed unselected vertex");
    }
    // Exact quarter-grid inputs avoid losing fractional bits when translating
    // the sampling frame by hundreds of units in binary32.
    if(&sample<samples+5) {
      mesh.VertPos(v).Init(sample.p[0],sample.p[1],sample.p[2]);matrix.l.Init(256,-256,512);
      mesh.Perlin(matrix,{1,1,1,0});
      for(unsigned c=0;c<3;++c) {
        const auto value=mesh.VertPos(v)[c];
        check((value==0 ? 0 : f(value))==sample.bits[c],"Perlin lattice must repeat every 256 units");
      }
    }
    matrix.l.x=std::numeric_limits<float>::infinity();
    rejects([&]{mesh.Perlin(matrix,{1,1,1,0});});
    check(std::fegetround()==rounding,"Perlin must restore rounding after an exception");
  }
  std::fesetenv(&previous);
  Fixture p;const auto base=p.add(0x81,cube(1,1,1,1));
  const auto a=p.get(base);
  const auto b=p.get(p.add(0x91,{0,0,0,0,0,0,0,0,0,0,f(1),f(2),f(3)},{base}));
  for(std::size_t i=0;i<a->vertices.size();++i)for(unsigned c=0;c<3;++c) {
    near(b->vertices[i].position[c],a->vertices[i].position[c]+(c==0 ? 1 : c==1 ? -2 : 0),"Perlin sampling SRT and per-axis amplitude");
  }
  seams(*b);
}
}
int main(int argc,char** argv) {
  try {
    check(argc==2,"expected Theta project");primitives();operators();normals();bends();indexed_operations();attributes_and_grids();displacement();noise_displacement();
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);
    for (const auto* name : {"mesh.cube","mesh.bwflattorus","mesh.LineCircle","mesh.FireGradientCircle",
        "mesh.bwflattorus8","mesh.bwflattorus4","mesh.flatfractal","mesh.g4flat","mesh.citybridge",
        "mesh.cityblock","mesh.rohrcylin120","mesh.rohrcylin32","msh.wfl1","mesh.tetra","mesh.tetra2","mesh.Arrow",
        "mesh.fireball","mesh.fireball2","mesh.highway1","mesh.highway2","mesh.stars","mesh.fireballBlackBody",
        "mesh.bridge2","mesh.citycomp","mesh.5roehrenstart","mesh.rohr160_1","mesh.rohr160_2","mesh.rohr160_3","mesh.rohr160_4","mesh.rohr160_5","mesh.g10walls","mesh.band"}) {
      const auto mesh=wz3::evaluate_mesh(project,graph,graph.names.at(name));
      wz3::validate_mesh(*mesh);check(!mesh->faces.empty(),"empty Theta mesh");
      wz3::mesh_normals(*mesh);
    }
    std::cout << "indexed mesh geometry, attributes, displacement and all 32 real Theta recipes passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
