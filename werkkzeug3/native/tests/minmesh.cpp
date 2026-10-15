// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_reference.hpp"
#include "wz3/scene.hpp"
#include "wz3/shadow.hpp"
#include <bit>
#include <iostream>
#include <set>

namespace {
using namespace wz3;
namespace mm=wz3::legacy::minimal;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(float a,float b,const char* message){check(std::abs(a-b)<1e-5f,message);}
std::uint32_t f(float v){return std::bit_cast<std::uint32_t>(v);}
template<class F> void rejects(F action){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid MINMESH accepted");}
struct Fixture {
  Project p;Graph g;
  std::size_t add(unsigned cmd,std::vector<std::uint32_t> args={},std::vector<std::size_t> inputs={},std::vector<std::optional<std::size_t>> links={}) {
    Operator op;op.command=cmd;op.parameters=std::move(args);p.operators.push_back(std::move(op));g.nodes.push_back({true,true,std::move(inputs),std::move(links)});return p.operators.size()-1;
  }
  auto get(std::size_t id){return evaluate_mesh(p,g,id);}
};
std::vector<std::uint32_t> cube(unsigned x=1,unsigned y=1,unsigned z=1,unsigned flags=0){return {x,y,z,flags,f(1),f(1),f(1),0,0,0,0,0,0};}
double volume(const Mesh& mesh) {
  double out=0;
  for(const auto& face:mesh.faces)for(unsigned c=2;c<face.count;++c) {
    const auto& a=mesh.vertices[face.vertices[0]].position;const auto& b=mesh.vertices[face.vertices[c-1]].position;const auto& d=mesh.vertices[face.vertices[c]].position;
    out+=(double(a[0])*(b[1]*d[2]-b[2]*d[1])+double(a[1])*(b[2]*d[0]-b[0]*d[2])+double(a[2])*(b[0]*d[1]-b[1]*d[0]))/6;
  }
  return out;
}
std::vector<std::uint8_t> all_shadow(const Mesh& mesh) {
  std::vector<std::uint8_t> mask;
  for(int i=0;i<mesh.min_kernel->Faces.Count;++i){const auto& face=mesh.min_kernel->Faces[i];mask.push_back(face.Cluster && face.Count>=3 && !(face.Flags&1));}
  return mask;
}
void primitives() {
  Fixture x;const auto id=x.add(0x102,cube());const auto a=x.get(id);
  check(a->min_kernel && !a->kernel && a->vertices.size()==24 && a->faces.size()==6,"MINMESH cube topology");
  near(volume(*a),1,"MINMESH cube winding");
  const auto bounds=mesh_bounds(*a);for(unsigned c=0;c<3;++c){near(bounds.min[c],-.5f,"cube lower bound");near(bounds.max[c],.5f,"cube upper bound");}
  const auto normals=mesh_normals(*a);
  const std::array<Vec3,6> expected={{{0,0,-1},{-1,0,0},{0,0,1},{1,0,0},{0,1,0},{0,-1,0}}};
  for(unsigned face=0;face<6;++face)for(unsigned corner=0;corner<4;++corner)check(normals[face][corner]==expected[face],"cube normal/seam orientation");
  const auto tess=x.get(x.add(0x102,cube(2,3,4)));check(tess->vertices.size()==94 && tess->faces.size()==52,"MINMESH cube ABI tessellation");near(volume(*tess),1,"cube tessellation volume");
  auto wrapped=cube(1,1,1,14);wrapped[4]=f(2);wrapped[5]=f(3);wrapped[6]=f(4);
  const auto scaled=x.get(x.add(0x102,wrapped));const auto box=mesh_bounds(*scaled);near(box.min[1],0,"cube bottom flag");near(box.max[1],3,"cube height");
  float u=0;for(const auto& v:scaled->vertices)u=std::max(u,v.uv[0]);near(u,8,"cube wraparound UV scale");
  const auto grid=x.get(x.add(0x101,{1,2,3}));check(grid->vertices.size()==24 && grid->faces.size()==12,"two-sided MINMESH grid");near(volume(*grid),0,"flat grid");
  const auto sphere=x.get(x.add(0x104,{8,3}));check(sphere->vertices.size()==38 && sphere->faces.size()==40 && sphere->min_kernel->Faces.Count==44,"sphere caps/stitch records");
  const auto& core=*sphere->min_kernel;
  for(unsigned y=0;y<=3;++y) {
    const auto& l=core.Vertices[y*9];const auto& r=core.Vertices[y*9+8];
    check(l.Pos.x==r.Pos.x && l.Pos.y==r.Pos.y && l.Pos.z==r.Pos.z,"sphere seam positions");
    check(l.Normal.x==r.Normal.x && l.Normal.y==r.Normal.y && l.Normal.z==r.Normal.z,"sphere seam stitches lost normal continuity");
    check(l.UV[0][0]==0 && r.UV[0][0]==1,"sphere UV seam collapsed");
  }
  near(core.Vertices[36].Pos.y,core.Vertices[0].Pos.y,"sphere cap height is not ring height");
  near(core.Vertices[37].Pos.y,core.Vertices[35].Pos.y,"sphere second cap height");
  for(const auto* mesh:{a.get(),tess.get(),sphere.get(),grid.get()})check_minmesh_shadow_reference(*mesh,all_shadow(*mesh));
  const auto shadow=prepare_shadow_mesh(*a,all_shadow(*a));check(shadow.edges.size()==12 && shadow.planes.size()==7 && shadow.triangles.size()==36,"closed MINMESH cube shadow topology");
  auto partial=all_shadow(*a);partial[0]=0;check_minmesh_shadow_reference(*a,partial);
}
void selection_and_clusters() {
  Fixture x;const auto base=x.add(0x102,cube());const auto material=x.add(0xd3);const auto alias=x.add(1,{}, {},{material});
  const auto selected=x.add(0x113,{6,f(.5f),0,0,0,f(2),f(2)},{base});
  const auto bound=x.add(0x110,{1,7},{selected},{material});const auto mesh=x.get(bound);
  unsigned count=0;for(const auto& face:mesh->faces)if(face.material){++count;check(face.pass==7,"selected material pass");}
  check(count==1,"face selection must select only the positive X face");
  const auto deleted=x.get(x.add(0x114,{}, {selected}));check(deleted->faces.size()==5,"delete selected faces");
  const auto all=x.add(0x110,{0,3},{base},{material});const auto again=x.get(x.add(0x110,{0,9},{all},{alias}));
  for(const auto& face:again->faces)check(face.pass==3 && face.material==material,"AddCluster must reuse material identity without comparing pass");
  const auto from_input=x.get(x.add(0x117,{0,5},{base,material}));for(const auto& face:from_input->faces)check(face.pass==5 && face.material==material,"MatInput binding");
  auto transform=std::vector<std::uint32_t>{0,f(1),f(1),f(1),0,0,0,f(2),0,0};
  const auto moved=x.add(0x120,transform,{base});const auto added=x.get(x.add(0x111,{}, {base,moved}));
  check(added->faces.size()==12 && added->min_kernel->Clusters.Count==2,"Add cluster merge");
  const auto bounds=mesh_bounds(*added);near(bounds.min[0],-.5f,"Add changed shared input");near(bounds.max[0],2.5f,"transformed copy missing");
  const auto inverted=x.get(x.add(0x115,{}, {base}));near(volume(*inverted),-1,"MINMESH inversion");
  const auto tri=x.get(x.add(0x12a,{}, {base}));check(tri->faces.size()==12,"MINMESH triangulation");near(volume(*tri),1,"MINMESH triangulation winding");
  const auto centered=x.get(x.add(0x131,{7},{moved}));near(mesh_bounds(*centered).min[0],-.5f,"MINMESH centering");
  transform[0]=36;transform[7]=f(.25f);transform[8]=f(.5f);transform.push_back(0);transform.push_back(0);
  const auto uv=x.get(x.add(0x121,transform,{base}));near(uv->vertices[0].uv[0],.25f,"TransEx source/destination UV");near(uv->vertices[0].uv[1],1.5f,"TransEx UV translation");
  Material m;MaterialPass pass;pass.program=MaterialProgram::shadow;m.passes.push_back(pass);
  const auto mask=shadow_face_mask(*mesh,{{material,m}});check(std::count(mask.begin(),mask.end(),1)==1,"MINMESH material shadow mask");
  check_minmesh_shadow_reference(*mesh,mask);
}
void conversion() {
  Fixture x;std::vector<std::size_t> inputs;
  for(unsigned flags:{0u,1u,2u,8u,15u})inputs.push_back(x.add(0x81,cube(2,3,4,flags)));
  inputs.push_back(x.add(0x84,{8,3}));inputs.push_back(x.add(0x9d,{0,3,5}));
  inputs.push_back(x.add(0x83,{8,8,f(1),f(.25f),0,f(1),0}));
  inputs.push_back(x.add(0x87,{0,f(1),1},{inputs[0]}));
  inputs.push_back(x.add(0xb0,{0},{inputs[0]}));
  for(auto input:inputs) {
    const auto before=x.get(input);const auto expected=minmesh_conversion_reference(*before->kernel);
    const auto actual=x.get(x.add(0xb5,{}, {input}));const auto& core=*actual->min_kernel;
    check(core.Vertices.Count==expected->Vertices.Count && core.Faces.Count==expected->Faces.Count,"conversion count differs from Mesh_ToMin");
    for(int i=0;i<core.Vertices.Count;++i) {
      const auto& a=core.Vertices[i];const auto& b=expected->Vertices[i];
      check(a.Pos.x==b.Pos.x && a.Pos.y==b.Pos.y && a.Pos.z==b.Pos.z && a.Color==b.Color && a.Select==b.Select,"conversion vertex differs from Mesh_ToMin");
      for(unsigned uv=0;uv<2;++uv)for(unsigned c=0;c<2;++c)check(a.UV[uv][c]==b.UV[uv][c],"conversion UV differs from Mesh_ToMin");
    }
    for(int i=0;i<core.Faces.Count;++i) {
      const auto& a=core.Faces[i];const auto& b=expected->Faces[i];
      check(a.Count==b.Count && a.Cluster==b.Cluster && a.Select==b.Select && a.Flags==b.Flags,"conversion face differs from Mesh_ToMin");
      for(unsigned c=0;c<a.Count;++c)check(a.Vertices[c]==b.Vertices[c],"conversion index differs from Mesh_ToMin");
    }
    if(input!=inputs.back())check_minmesh_shadow_reference(*actual,all_shadow(*actual));
    else {const auto mask=all_shadow(*actual);check(std::count(mask.begin(),mask.end(),1)==0,"converted ShadowEnable flag lost");}
  }
}
void topology_and_limits() {
  mm::GenMinMesh core;core.Vertices.Resize(4);
  core.Vertices[1].MergeTag=1;core.Vertices[2].Pos.x=-0.f;core.Vertices[3].Weights[3]=1;
  std::unique_ptr<int[]> map(core.CalcMergeVerts());
  check(map[0]==0 && map[1]==0 && map[2]==2 && map[3]==3,"MINMESH bitwise weld must ignore MergeTag but preserve signed zero and weights");
  Fixture x;rejects([&]{x.get(x.add(0x102,cube(0)));});rejects([&]{x.get(x.add(0x104,{0,1}));});
  const auto mesh=x.add(0x101,{0,1,1});rejects([&]{x.get(x.add(0x120,{0,f(1),f(1),f(1),0,0,0,f(INFINITY),0,0},{mesh}));});
  rejects([&]{x.get(x.add(0x110,{0,0},{mesh},{mesh}));});
  const auto cycle=x.add(1);x.g.nodes[cycle].links={cycle};rejects([&]{x.get(cycle);});
  core.Faces.Resize(1);core.Faces[0].Count=9;rejects([&]{core.validate();});
  rejects([&]{core.Vertices.Resize(1<<21);});
}
void scene_integration() {
  Fixture x;const auto cube_id=x.add(0x102,cube());const auto alias=x.add(2,{}, {cube_id});
  const auto pass=x.add(0x180,{7},{alias});
  const auto view=x.add(0xf0,{3,3,0xff000000,0,0,0,0,0,f(-5),f(4096),f(.125f),0,0,f(1),f(1),0xff000000,f(4096),f(16)},{pass});
  auto assets=std::make_shared<SceneAssets>(x.p,x.g);ScenePlayer player(x.p,x.g,{},assets);
  const auto a=player.viewport(view,frame_environment(0),1);const auto b=player.viewport(view,frame_environment(65536),1);
  check(a.instances.size()==1 && a.instances[0].mesh->min_kernel && a.instances[0].mesh==b.instances[0].mesh,"MINMESH scene dispatch/cache");
  check(a.instances[0].pass_adjust==7,"MINMESH scene pass wrapper was skipped");
  check(assets->stats().mesh_bytes==mesh_storage_bytes(*a.instances[0].mesh),"MINMESH cache accounting");
}
void compact_storage() {
  Fixture x;const auto original=x.get(x.add(0x102,cube(2,3,4)));
  auto core=std::make_shared<mm::GenMinMesh>(*original->min_kernel);
  core->Vertices.AtLeast(16384);core->Faces.AtLeast(16384);
  const auto oversized=core->bytes();const auto packed=mm::export_mesh(core);
  check(core->bytes()<oversized/100,"immutable MINMESH kept construction capacity");
  check(core->Vertices.Array==core->Vertices.storage.data() && core->Faces.Array==core->Faces.storage.data(),"MINMESH compaction left dangling pointers");
  check(mesh_normals(packed)==mesh_normals(*original),"MINMESH compaction changed normals");
  check_minmesh_shadow_reference(packed,all_shadow(packed));
  auto copy=std::make_shared<mm::GenMinMesh>(*packed.min_kernel);copy->Add(packed.min_kernel.get());
  const auto doubled=mm::export_mesh(copy);check(doubled.faces.size()==2*packed.faces.size(),"compacted MINMESH cannot grow again");
  const auto empty=mm::export_mesh(std::make_shared<mm::GenMinMesh>());
  check(empty.vertices.empty() && empty.faces.empty() && empty.min_kernel->Vertices.storage.size()==1 && empty.min_kernel->Faces.storage.size()==1,"MINMESH compaction lost empty-array sentinels");
}
void debris(const char* path) {
  const auto p=read_project(path);const auto g=connect(p);const auto mesh=evaluate_mesh(p,g,925);
  check(mesh->vertices.size()==1014 && mesh->faces.size()==864,"Debris first MINMESH object changed topology");
  const auto box=mesh_bounds(*mesh);for(unsigned c=0;c<3;++c){near(box.min[c],-6,"Debris object lower bound");near(box.max[c],6,"Debris object upper bound");}
  for(const auto& face:mesh->faces)check(face.material==8462 && face.pass==0,"Debris MatLink binding lost");
  check_minmesh_shadow_reference(*mesh,all_shadow(*mesh));
  unsigned converted=0;std::map<std::string,unsigned> unsupported;
  for(std::size_t i=0;i<p.operators.size();++i)if(g.nodes[i].reachable && operator_info(p.operators[i].command).category=="MINMESH") {
    try {const auto value=evaluate_mesh(p,g,i);validate_mesh(*value);++converted;}
    catch(const std::runtime_error& e) {
      const std::string message=e.what();const auto begin=message.find('('),end=message.find(')',begin);
      if(begin==std::string::npos || end==std::string::npos || !message.ends_with(" is not ported"))throw;
      const auto name=message.substr(begin+1,end-begin-1);
      const std::set<std::string> remaining={"Perlin Displace","BoneChain","BoneTrain","Multiply","Font3D","Pipe"};
      if(!remaining.contains(name))throw;
      ++unsupported[name];
    }
  }
  check(converted>=277,"real Debris MINMESH coverage regressed");
  std::cout<<converted<<" reachable Debris MINMESH operators evaluate; remaining dependencies:\n";
  for(const auto& [message,count]:unsupported)std::cout<<count<<" blocked: "<<message<<'\n';
}
}
int main(int argc,char** argv) {
  try{primitives();selection_and_clusters();conversion();topology_and_limits();scene_integration();compact_storage();if(argc>1)debris(argv[1]);}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
