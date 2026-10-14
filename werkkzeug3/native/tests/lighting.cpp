// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/lighting.hpp"
#include "wz3/scene_draw.hpp"
#include "mesh_kernel.hpp"
#include <iostream>
#include <cfenv>
namespace source {
using namespace wz3::legacy;
using sU8=std::uint8_t;using sU16=std::uint16_t;
int sFtol(float v){return int(std::nearbyint(v));}
struct D3DLOCKED_RECT {int Pitch;void* pBits;};
struct D3DLOCKED_BOX {int SlicePitch,RowPitch;void* pBits;};
using D3DCUBEMAP_FACES=int;
constexpr int D3DFMT_A8R8G8B8=1,D3DFMT_Q8W8V8U8=5,D3DPOOL_MANAGED=0;
struct Cube {
  int size;std::vector<sU8> data;
  explicit Cube(int s):size(s),data(6*s*s*4){}
  void LockRect(int face,int,D3DLOCKED_RECT* r,int,int){r->Pitch=size*4;r->pBits=data.data()+face*size*size*4;}
  void UnlockRect(int,int){}
};
struct Volume {
  int size;std::vector<sU8> data;
  explicit Volume(int s):size(s),data(s*s*s*4){}
  void LockBox(int,D3DLOCKED_BOX* b,int,int){b->RowPitch=size*4;b->SlicePitch=size*size*4;b->pBits=data.data();}
  void UnlockBox(int){}
};
struct Device {
  void CreateCubeTexture(int s,int,int,int,int,Cube** result,int){*result=new Cube(s);}
  void CreateVolumeTexture(int s,int,int,int,int,int,int,Volume** result,int){*result=new Volume(s);}
} device;
struct sSystem_ {
  Device* DXDev=&device;Cube *DXNormalCube{},*DXTinyNormalCube{};Volume* DXAttenuationVolume{};
  ~sSystem_(){delete DXNormalCube;delete DXTinyNormalCube;delete DXAttenuationVolume;}
  void MakeCubeNormalizer();void MakeAttenuationVolume();
};
#define DXERROR(x) x
#include "lighting_oracle.inc"
#undef DXERROR
}
namespace {
void tables() {
  source::sSystem_ archived;archived.MakeCubeNormalizer();archived.MakeAttenuationVolume();
  const auto actual=wz3::lighting_tables();check(actual.cube.size()==archived.DXNormalCube->data.size(),"normalizer cube size");
  for(std::size_t i=0;i<actual.cube.size();i+=4)for(unsigned c=0;c<4;++c)check(actual.cube[i+c]==archived.DXNormalCube->data[i+(c<3 ? 2-c : c)],"normalizer cube differs from archived generator");
  check(actual.attenuation==archived.DXAttenuationVolume->data,"attenuation volume differs from archived generator");
  std::cout<<"24,576 cube texels and 32,768 volume texels match archived source\n";
}
void normals() {
  wz3::Texture t;t.width=256;t.height=16;t.format=5;t.mip_count=1;t.pixels.resize(t.width*t.height);
  std::uint32_t state=1;
  for(auto& p:t.pixels)for(auto& c:p){state=state*1664525+1013904223;c=(state>>16)&0x7fff;}
  t.pixels[0]={16384,16384,16384,65535};const auto levels=wz3::signed_normal_levels(t);check(levels.size()==1,"signed mip count ignored");
  for(std::size_t i=0;i<t.pixels.size();++i){const auto& p=t.pixels[i];const std::uint16_t bgra[]={p[2],p[1],p[0],p[3]};const auto expected=source::signed_normal(bgra);for(unsigned c=0;c<4;++c)check(std::uint8_t(levels[0].pixels[i*4+c])==((expected>>(8*c))&255),"signed normal differs from archived upload");}
  t.width=t.height=2;t.pixels={{32767,16384,16384,65535},{16384,32767,16384,65535},{16384,16384,32767,65535},{16384,16384,16384,65535}};t.mip_count=0;
  const auto filtered=wz3::signed_normal_levels(t);check(filtered.size()==2 && filtered[1].pixels[0]==73 && filtered[1].pixels[1]==73 && filtered[1].pixels[2]==73 && filtered[1].pixels[3]==127,"normal mip must average 15-bit data before signed normalization");
  t.mip_threshold=1;const auto point=wz3::signed_normal_levels(t);check(point[1].pixels[0]==127 && point[1].pixels[1]==0,"unfiltered normal mip threshold");
  t.mip_threshold=16;check(wz3::signed_normal_levels(t)[1].pixels==point[1].pixels,"reversed mip threshold");
}
void selection() {
  wz3::SceneFrame scene;
  for(unsigned i=0;i<20;++i)scene.lights.push_back({i,{0,0,5},{0,0,1},0,0xffffffff,1,float(i+1)});
  scene.lights.push_back({100,{0,0,-1000},{},0,0xffffffff,1,1});scene.lights.push_back({101,{0,0,5},{},0,0xffffffff,0,10000});
  auto selected=wz3::scene_lights(scene,64,64);check(selected.size()==16 && selected.front().light.operator_id==19 && selected.back().light.operator_id==4,"light range/distance selection or cap");
  scene.lights.clear();const auto fallback=wz3::scene_lights(scene,64,64);check(fallback.size()==1 && fallback[0].light.range==32 && fallback[0].light.color==0x78787878,"Theta fallback light");
  scene.camera.near_clip=64;check(wz3::scene_lights(scene,64,64).empty(),"fallback light must obey the near frustum plane");scene.camera.near_clip=.125f;
  scene.lights={{1,{0,0,5},{},0,0xffffffff,1,1},{2,{0,0,5},{},0,0xffffffff,1,1}};selected=wz3::scene_lights(scene,64,64);check(selected[0].light.operator_id==1,"equal-importance light order");
  near(selected[0].rectangle[0],-1/std::sqrt(24.f),"sphere scissor tangent");near(selected[0].rectangle[2],1/std::sqrt(24.f),"sphere scissor tangent");
  auto matrix=wz3::scene_identity();matrix[0]=2;matrix[12]=3;check(wz3::light_model_vector(matrix,{5,4,3},true)==wz3::Vec3{4,4,3},"archived TransR must retain scaled-model behavior");
  near(wz3::light_specular_parameters(0)[0],wz3::light_specular_parameters(8)[0],"specular lower clamp");near(wz3::light_specular_parameters(100)[0],wz3::light_specular_parameters(60)[0],"specular upper clamp");
}
void jobs() {
  auto mesh=std::make_shared<wz3::Mesh>();mesh->vertices={{{-1,1,2}},{{1,1,2}},{{0,-1,2}}};wz3::MeshFace f;f.count=3;f.vertices={0,1,2,0};f.material=0;mesh->faces={f};
  wz3::SceneFrame scene;scene.instances={{0,mesh}};auto& m=scene.materials[0];m.material11_insert=true;
  wz3::MaterialPass base,shadow,light,post;base.usage=wz3::MaterialUsage::base;shadow.usage=wz3::MaterialUsage::shadow;shadow.program=wz3::MaterialProgram::shadow;light.usage=wz3::MaterialUsage::light;post.usage=wz3::MaterialUsage::postlight;m.passes={base,shadow,light,post};
  scene.lights={{0,{0,0,0},{0,0,1},0,0xffffffff,1,10},{1,{1,0,0},{0,0,1},0,0xffffffff,1,8}};
  const auto draws=wz3::scene_draw_jobs(scene,64,64);check(draws.jobs.size()==4 && draws.inserts[0],"base/light/post jobs and material insert registration");
  check(!draws.jobs[0].light && draws.jobs[1].light==0 && draws.jobs[2].light==1 && !draws.jobs[3].light,"per-light duplication and phase ordering");
  scene.lights[0].flags=2;const auto shadowed=wz3::scene_draw_jobs(scene,64,64);check(shadowed.jobs.size()==5 && ((shadowed.jobs[1].sort_key>>16)&15)==3,"required shadow job missing");
  scene.lights[0].range=.25f;check(wz3::scene_draw_jobs(scene,64,64).jobs.size()==3,"distant light must not illuminate batch or require its shadow");
  scene.viewport_flags=0x10;check(wz3::scene_draw_jobs(scene,64,64).jobs.empty(),"simple paint mode must select only OTHER mesh usage");
  scene.viewport_flags=0;scene.instances[0].world[12]=1000;check(wz3::scene_draw_jobs(scene,64,64).jobs.empty(),"offscreen mesh batch culling");
}

}
int main(){try{tables();normals();selection();jobs();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
