// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/scene_draw.hpp"
#include <cfenv>
#include <iostream>
namespace {
void queue() {
  auto mesh=std::make_shared<wz3::Mesh>();
  mesh->vertices={{{-1,-1,2}},{{1,-1,2}},{{0,1,2}}};
  wz3::MeshFace face;face.count=3;face.vertices={0,1,2,0};face.material=12;face.pass=255;
  mesh->faces={face,face};mesh->faces[1].pass=-2;
  wz3::SceneFrame scene;scene.instances={{0,mesh,wz3::scene_identity(),0,3},{0,mesh,wz3::scene_identity(),0,3}};
  wz3::MaterialPass pass;pass.render_pass=2;scene.materials[12].passes={pass};
  wz3::SceneEffectDraw effect;effect.sort_key=(4u<<24)|(7u<<16);scene.effects={effect};
  const auto list=wz3::scene_unlit_jobs(scene);
  check(list.meshes.size()==1 && list.meshes.at(mesh.get()).size()==2,"face pass split/shared mesh batching");
  check(list.jobs.size()==5,"missing ordinary draw jobs");
  check(list.jobs[0].sort_key>>24==3 && list.jobs[0].instance==1 && list.jobs[0].batch==1,"wrap signed face pass before clamping instance adjustment");
  check(list.jobs[1].instance==0,"same-material jobs retain reverse encounter order");
  check(list.jobs[2].effect==0 && list.jobs[3].sort_key>>24==4,"effect light-zero slot precedes ordinary mesh light-fifteen slot");
  check(list.jobs[3].instance==1 && list.jobs[4].instance==0,"mesh order within second pass");
  scene.instances[0].pass_adjust=-500;scene.instances[1].pass_adjust=500;
  const auto clamped=wz3::scene_unlit_jobs(scene);check(clamped.jobs.front().sort_key>>24==0 && clamped.jobs.back().sort_key>>24==255,"instance pass clamp");
  scene.materials[12].passes[0].usage=wz3::MaterialUsage::base;
  const auto base=wz3::scene_unlit_jobs(scene);check((base.jobs.front().sort_key&0x00ff0000)==0,"base phase precedes all light slots");
  for(unsigned failure=0;failure<4;++failure) {
    auto broken=scene;
    if(failure==0)broken.materials.clear();
    if(failure==1)broken.materials[12].material11_insert=true;
    if(failure==2)broken.materials[12].passes[0].usage=wz3::MaterialUsage::light;
    if(failure==3)broken.materials[12].passes[0].program=wz3::MaterialProgram::sprites;
    bool rejected=false;try {wz3::scene_unlit_jobs(broken);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"unimplemented scene material behavior was silently skipped");
  }
}
void colors() {
  for(auto mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
    std::fesetround(mode);
    check(wz3::mesh_vertex_color({.5f,2,-1,1})==0xff80ff00,"mesh ARGB conversion/nearest-even rounding");
    check(std::fegetround()==mode,"mesh color conversion leaked rounding mode");
  }
  std::fesetround(FE_TONEAREST);
  for(unsigned i=0;i<256;++i) {const auto v=float(i)/255;check(wz3::mesh_vertex_color({v,v,v,v})==i*0x01010101u,"8-bit vertex color roundtrip");}
}
}
int main() {try {queue();colors();std::cout<<"Mesh batch, phase/pass ordering and packed vertex colors passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
