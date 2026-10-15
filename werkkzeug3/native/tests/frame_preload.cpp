// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/frame_preload.hpp"
#include "wz3/scene_draw.hpp"
#include <iostream>
namespace {
void visible_resources() {
  FrameFixture fixture;const auto vp=fixture.viewport(4);
  wz3::FramePlayer player(fixture.p,fixture.g);auto frame=player.frame(vp,0,128,96);
  auto& scene=std::get<wz3::SceneFrame>(frame.commands[0].draw);
  scene.instances[0].world[12]=10000;
  wz3::FramePreloader preload;
  check(preload.needs_draw(frame),"first target allocation missed");
  check(!preload.needs_draw(frame),"unchanged hidden mesh causes redraw");
  scene.instances[0].world[12]=0;
  check(preload.needs_draw(frame),"previously hidden mesh entering view was not warmed");
  scene.instances[0].world[12]=.1f;scene.lights[0].amplify=.3f;
  check(!preload.needs_draw(frame),"transform/light uniforms cause preparation draw");
  auto other=std::make_shared<wz3::Mesh>(*scene.instances[0].mesh);
  scene.instances.push_back({1,other});
  check(preload.needs_draw(frame),"single-frame mesh appearance missed");
  scene.instances.pop_back();check(!preload.needs_draw(frame),"mesh disappearance causes redraw");
  // Preparation owns immutable meshes until done, so an allocator cannot
  // recycle a pointer and mistake new geometry for a warmed mesh.
  std::weak_ptr<const wz3::Mesh> retained=other;other.reset();check(!retained.expired(),"preload mesh identity can dangle");
  scene.instances[0].pass_adjust=4;check(!preload.needs_draw(frame),"pass ordering alone creates no resource");
  frame.surfaces[0]={256,192};check(preload.needs_draw(frame),"target growth missed");
  frame.surfaces[0]={128,96};check(preload.needs_draw(frame),"return to previously used size must reallocate");
}
void shader_changes() {
  FrameFixture fixture;const auto layer=fixture.layer(fixture.material(0xffff0000),4);
  wz3::FramePlayer player(fixture.p,fixture.g);auto frame=player.frame(layer,0,128,96);
  auto& quad=std::get<wz3::FrameQuad>(frame.commands.back().draw);wz3::FramePreloader preload;
  check(preload.needs_draw(frame) && !preload.needs_draw(frame),"quad preload repetition");
  quad.shader.words[28]=0xff00ff00;quad.shader.words[46]=f(.2f);quad.rectangle[0]=.2f;
  check(!preload.needs_draw(frame),"quad animated uniforms cause preparation draw");
  quad.shader.words[0]|=1;check(preload.needs_draw(frame),"new alpha-test shader missed");
  quad.shader.textures[0]=10;quad.textures[0]=wz3::FrameTexture{false,10};quad.shader.words[11]=0x40;
  check(preload.needs_draw(frame),"new sampler missed");
  quad.shader.textures[0]=11;quad.textures[0]->id=11;
  check(preload.needs_draw(frame),"texture identity change missed");
  quad.released_layer=true;quad.rectangle[2]=quad.rectangle[0];quad.textures[0]->id=12;
  check(!preload.needs_draw(frame),"empty released layer warmed an undrawn texture");
  quad.rectangle[2]=1;check(preload.needs_draw(frame),"layer becoming nonempty missed");
}
void cached_culling() {
  FrameFixture fixture;const auto vp=fixture.viewport(4);wz3::FramePlayer player(fixture.p,fixture.g);
  const auto frame=player.frame(vp,0,128,96);auto scene=std::get<wz3::SceneFrame>(frame.commands[0].draw);
  wz3::SceneDrawCache cache;
  for(float offset:{0.f,2.f,10.f,-10.f,10000.f,0.f}) {
    scene.instances[0].world[12]=offset;
    const auto fresh=wz3::scene_draw_jobs(scene,128,96),cached=wz3::scene_draw_jobs(scene,128,96,&cache);
    check(fresh.jobs.size()==cached.jobs.size(),"cached local bounds changed visibility");
    for(std::size_t i=0;i<fresh.jobs.size();++i) {
      const auto& a=fresh.jobs[i];const auto& b=cached.jobs[i];
      check(a.sort_key==b.sort_key && a.instance==b.instance && a.batch==b.batch && a.material_pass==b.material_pass && a.light==b.light && a.shadow_zfail==b.shadow_zfail,"cached batching changed draw order");
    }
  }
}
void instance_capacity() {
  FrameFixture fixture;const auto vp=fixture.viewport(4);wz3::FramePlayer player(fixture.p,fixture.g);
  auto frame=player.frame(vp,0,128,96);auto& scene=std::get<wz3::SceneFrame>(frame.commands[0].draw);
  auto mesh=std::make_shared<wz3::Mesh>(*scene.instances[0].mesh);for(auto& face:mesh->faces)face.material=0;
  scene.instances[0].mesh=mesh;wz3::MaterialPass pass;pass.program=wz3::MaterialProgram::instances;scene.materials[0].passes={pass};
  wz3::FramePreloader preload;std::size_t capacity=0;
  for(std::size_t count:{1,2,3,4,5,6,7,8,9,8,4}) {
    scene.instances[0].particle_matrices=std::make_shared<const std::vector<wz3::SceneMatrix>>(count,wz3::scene_identity());
    const bool grew=count>capacity;check(preload.needs_draw(frame)==grew,"particle capacity preparation must follow geometric GPU growth");
    if(grew){capacity=1;while(capacity<count)capacity*=2;}
  }
}
}
int main(){try{visible_resources();shader_changes();cached_culling();instance_capacity();std::cout<<"Preparation tracks visibility, transient geometry, shader/texture changes, particle capacity and resized targets; cached culling matches fresh batches\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
