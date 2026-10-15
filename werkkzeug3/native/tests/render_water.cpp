// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/frame_preload.hpp"
#include "spline_kernel.hpp"
#include <algorithm>
#include <iostream>
namespace {
using namespace wz3;
DemoFrame frame(SceneFrame scene,std::size_t target) {
  DemoFrame out;out.width=128;out.height=96;out.surfaces={{0,{128,96}},{target,{64,64}}};out.commands.push_back({0,{0,128,96},std::move(scene)});return out;
}
void pattern(FrameRenderer& renderer,std::size_t target) {
  auto out=frame({},target);out.commands.clear();
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
    FrameQuad q;q.rectangle={x/16.f,y/16.f,(x+1)/16.f,(y+1)/16.f};q.shader.words[0]=0x20002;q.shader.words[9]=0x10;
    q.shader.words[28]=0xff000000|(x*17<<16)|(y*17<<8)|((x*13+y*7)&255);out.commands.push_back({0,{target,64,64},q});
  }
  renderer.render(out);
}
SceneEffectDraw effect(std::size_t bitmap,unsigned grid,float time) {
  SceneEffectDraw out;auto& water=out.water.emplace();water.parameters={f(8),f(1),f(1),f(1),f(1),grid};water.time=time;
  auto spline=std::make_shared<legacy::BlobSpline>();spline->Keys={{0,.1f,-.1f,0,.1f,2,2.5f,1.25f,.2f}};spline->Count=1;water.spline=spline;
  out.material_model=scene_srt(std::array<std::uint32_t,9>{f(.9f),f(1.1f),f(1),f(.19f),f(.04f),f(.01f),f(.2f),f(-.1f),f(8)});
  out.camera_world=scene_srt(std::array<std::uint32_t,9>{f(1),f(1),f(1),f(.01f),f(-.02f),0,f(-.1f),f(.2f),0});
  out.shader.words[0]=0x302;out.shader.words[1]=0x20;out.shader.words[17]=0x10;out.shader.words[6]=0x101;out.shader.textures[2]=bitmap;return out;
}
// Independently evaluate material11.vsh's reflection operations, then draw an
// ordinary mesh with baked UVs. This checks Water's indexed FVF layout, supplied
// normals, model/camera uniforms and triangle order through a separate GPU path.
std::shared_ptr<Mesh> reference(const SceneEffectDraw& draw,SceneCamera camera) {
  const auto water=water_geometry(*draw.water);auto mesh=std::make_shared<Mesh>();
  if(draw.camera_world)camera.world=*draw.camera_world;
  auto view=scene_identity();for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)view[c*4+r]=camera.world[r*4+c];
  for(unsigned r=0;r<3;++r)view[12+r]=-(view[r]*camera.world[12]+view[4+r]*camera.world[13]+view[8+r]*camera.world[14]);
  const auto model_view=scene_combine(view,draw.material_model);
  for(std::size_t i=0;i<water.vertices.size();i+=16) {
    const auto* p=water.vertices.data()+i;MeshVertex vertex;vertex.position={p[0],p[1],p[2]};vertex.uv={p[8],p[9],0};
    const auto color=std::bit_cast<std::uint32_t>(p[6]);vertex.color={((color>>16)&255)/255.f,((color>>8)&255)/255.f,(color&255)/255.f,(color>>24)/255.f};
    if(draw.shader.words[1]==0x20) {
      auto eye=scene_point(model_view,vertex.position);const float length=std::sqrt(eye[0]*eye[0]+eye[1]*eye[1]+eye[2]*eye[2]);for(auto& c:eye)c/=length;
      Vec3 normal;for(unsigned r=0;r<3;++r)normal[r]=model_view[r]*p[3]+model_view[4+r]*p[4]+model_view[8+r]*p[5];
      const float dot=eye[0]*normal[0]+eye[1]*normal[1]+eye[2]*normal[2],square=normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2];
      const float w=dot*(2/square);vertex.uv={(normal[0]*w-eye[0])*.5f+.5f,(normal[1]*w-eye[1])*-.5f+.5f,0};
    }
    mesh->vertices.push_back(vertex);
  }
  for(std::size_t i=0;i<water.indices.size();i+=3) {MeshFace face;face.count=3;face.material=0;for(unsigned c=0;c<3;++c)face.vertices[c]=water.indices[i+c];mesh->faces.push_back(face);}
  return mesh;
}
void compare(FrameRenderer& renderer,const SceneEffectDraw& draw,std::size_t target) {
  SceneFrame scene;scene.viewport_flags=3;scene.effects={draw};const auto actual=renderer.render(frame(scene,target));
  scene.effects.clear();scene.instances={{0,reference(draw,scene.camera),draw.material_model}};
  if(draw.camera_world)scene.camera.world=*draw.camera_world;
  MaterialPass pass;pass.shader=draw.shader;pass.shader.words[1]=0;scene.materials[0].passes={pass};
  const auto expected=renderer.render(frame(scene,target));unsigned maximum=0,visible=0;
  for(std::size_t i=0;i<actual.size();++i){maximum=std::max(maximum,unsigned(std::abs(int(actual[i])-expected[i])));if(i%4!=3)visible+=actual[i]!=0;}
  check(visible>200,"Water fixture is invisible");
  if(maximum>2)throw std::runtime_error("Water indexed render differs from baked geometry: "+std::to_string(maximum));
}
void tests() {
  FrameFixture x;const auto bitmap=x.add(0x26,{6,6,1}),target=13+bitmap;FrameRenderer renderer(x.p,x.g);pattern(renderer,target);
  for(unsigned grid:{3u,5u,7u})for(float time:{0.f,.3f,.8f})compare(renderer,effect(bitmap,grid,time),target);
  auto draw=effect(bitmap,3,.4f);draw.shader.words[1]=0;compare(renderer,draw,target); // Direct Water UVs.
  draw.shader.textures={};draw.shader.words[17]=0;draw.shader.words[19]=0x10;compare(renderer,draw,target); // Packed ARGB.
  check(renderer.stats().water_uploads==3,"Water index grids must be uploaded once per size");
  draw=effect(bitmap,5,.3f);
  SceneFrame scene;scene.viewport_flags=3;scene.effects={draw};const auto before=renderer.render(frame(scene,target));const auto warmed=renderer.stats();
  scene.effects[0].water->time=.7f;check(renderer.render(frame(scene,target))!=before,"Water did not update animated vertices");
  scene.effects[0].water->time=.3f;renderer.reset_targets();pattern(renderer,target);check(renderer.render(frame(scene,target))==before,"Water pixels retained history after reset");
  check(renderer.stats().water_uploads==warmed.water_uploads && renderer.stats().program_compilations==warmed.program_compilations,"Water reallocated indices or shaders during animation/reset");
  scene.effects[0].water->parameters[5]=8;renderer.render(frame(scene,target));check(renderer.stats().water_uploads==4,"Water 16-bit maximum grid failed");
  auto demo=frame(scene,target);FramePreloader preload;check(preload.needs_draw(demo) && !preload.needs_draw(demo),"Water preload repetition");
  auto& water=*std::get<SceneFrame>(demo.commands[0].draw).effects[0].water;water.time=.9f;
  check(!preload.needs_draw(demo),"animated Water time must not force preload drawing");water.parameters[5]=7;
  check(preload.needs_draw(demo),"Water grid change must warm a new index buffer");
  std::cout<<"Water reflections, direct UVs and colors match baked triangle meshes; animation/reset/preload retain GPU resources\n";
}
}
int main(){try{tests();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
