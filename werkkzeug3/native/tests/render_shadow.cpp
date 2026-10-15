// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/scene_draw.hpp"
#include "minmesh_kernel.hpp"
#include <iostream>
namespace {
wz3::Material material(bool casts) {
  wz3::Material m;m.material11_insert=true;wz3::MaterialPass base,light,shadow;
  base.usage=wz3::MaterialUsage::base;auto& b=base.shader.words;b[0]=0x20300;b[9]=0x10;b[28]=0x00202020;b[23]=13;
  light.usage=wz3::MaterialUsage::light;auto& l=light.shader.words;l[0]=0x302280;l[1]=0x40;l[2]=5;l[28]=0x00a0a0a0;l[32]=f(8);
  shadow.usage=wz3::MaterialUsage::shadow;shadow.program=wz3::MaterialProgram::shadow;shadow.shader.words[0]=0x70220;
  m.passes={base,light};if(casts)m.passes.push_back(shadow);return m;
}
wz3::Material material20(bool casts) {
  auto m=material(casts);const auto shadow=m.passes.back();m.passes.clear();m.material11_insert=false;m.material20_insert=true;
  for(unsigned i=0;i<4;++i) {
    wz3::MaterialPass p;p.usage=wz3::MaterialUsage(i==3 ? 4 : i);auto& s=p.shader.material20.emplace();
    s.stage=wz3::Material20Stage(i);s.parameters[1]=0xffa0a0a0;s.compiled_parameters=s.parameters;m.passes.push_back(p);
  }
  if(casts)m.passes.push_back(shadow);return m;
}
std::shared_ptr<wz3::Mesh> wall() {
  auto mesh=std::make_shared<wz3::Mesh>();mesh->vertices={{{-10,10,6}},{{10,10,6}},{{10,-10,6}},{{-10,-10,6}}};
  for(auto& v:mesh->vertices)v.tangent={1,0,0};wz3::MeshFace f;f.vertices={0,1,2,3};f.material=1;mesh->faces={f};return mesh;
}
wz3::SceneFrame scene(FrameFixture& f) {
  f.material(0xffffffff);
  const auto cube=f.add(0x81,{1,1,1,1,::f(1),::f(1),::f(1),0,0,0,0,0,0}),bound=f.add(0x96,{0,0},{cube},{0});
  auto mesh=wz3::evaluate_mesh(f.p,f.g,bound);wz3::SceneFrame s;s.viewport_flags=3;s.materials[0]=material(true);s.materials[1]=material(false);
  s.instances={{0,mesh},{0,wall()}};s.instances[0].world[14]=3;s.lights={{0,{-2,2,0},{},2,0xffffffff,1,20}};return s;
}
std::vector<std::uint8_t> render(wz3::FrameRenderer& renderer,wz3::SceneFrame s) {
  wz3::DemoFrame f;f.width=f.height=128;f.surfaces[0]={128,128};f.commands.push_back({0,{0,128,128},std::move(s)});return renderer.render(f);
}
bool hits(wz3::Vec3 origin,wz3::Vec3 target,wz3::Bounds box) {
  float enter=0,leave=1;
  for(unsigned c=0;c<3;++c) {
    const auto d=target[c]-origin[c];if(std::abs(d)<1e-8f){if(origin[c]<box.min[c] || origin[c]>box.max[c])return false;continue;}
    auto a=(box.min[c]-origin[c])/d,b=(box.max[c]-origin[c])/d;if(a>b)std::swap(a,b);enter=std::max(enter,a);leave=std::min(leave,b);
  }
  return enter<leave;
}
void check_receiver(const wz3::SceneFrame& scene,const std::vector<std::uint8_t>& lit,const std::vector<std::uint8_t>& shaded,const std::vector<std::uint8_t>* additional=nullptr) {
  unsigned shadowed=0,clear=0;const wz3::Vec3 eye={scene.camera.world[12],scene.camera.world[13],scene.camera.world[14]};
  for(unsigned y=2;y<126;++y)for(unsigned x=2;x<126;++x) {
    const float z=6-eye[2];const wz3::Vec3 p={eye[0]+(2*(x+.5f)/128-1)*z+1.f/128,eye[1]+(1-2*(y+.5f)/128)*z-1.f/128,6};
    if(std::abs(p[0])>9 || std::abs(p[1])>9 || hits(eye,p,{{-.6f,-.6f,2.4f},{.6f,.6f,3.6f}}))continue;
    const bool inner=hits(scene.lights[0].position,p,{{-.4f,-.4f,2.6f},{.4f,.4f,3.4f}}),outer=hits(scene.lights[0].position,p,{{-.6f,-.6f,2.4f},{.6f,.6f,3.6f}});
    if(!inner && outer)continue;const auto at=(y*128+x)*4;
    for(unsigned c=0;c<3;++c) {
      const int expected=inner ? (additional ? (*additional)[at+c] : 32) : lit[at+c];
      if(std::abs(int(shaded[at+c])-expected)>2)throw std::runtime_error("shadow receiver mismatch at "+std::to_string(x)+","+std::to_string(y)+" expected "+std::to_string(expected)+", got "+std::to_string(shaded[at+c]));
    }
    shadowed+=inner;clear+=!outer;
  }
  check(shadowed>30 && clear>100,"shadow fixture lacks clear and occluded receiver pixels");
}
void controlled() {
  for(bool m20:{false,true}) {
  FrameFixture fixture;auto s=scene(fixture);if(m20){s.materials[0]=material20(true);s.materials[1]=material20(false);s.ambient=0xff202020;}
  wz3::FrameRenderer renderer(fixture.p,fixture.g);
  for(bool inside:{false,true}) {
    if(inside){s.camera.world[12]=1;s.camera.world[13]=-1;s.camera.world[14]=4.5f;}
    const auto jobs=wz3::scene_draw_jobs(s,128,128);unsigned shadows=0;
    for(const auto& j:jobs.jobs)if(((j.sort_key>>16)&15)==3){check(j.shadow_zfail==inside,"unexpected stencil algorithm for fixture");++shadows;}check(shadows==1,"shared shadow job missing");
    auto unshadowed=s;unshadowed.lights[0].flags=0;const auto lit=render(renderer,unshadowed),shaded=render(renderer,s);check_receiver(s,lit,shaded);
    // Coincident volumes exercise wrapping increment/decrement rather than clamping.
    auto overlap=s;overlap.instances.push_back(overlap.instances[0]);check_receiver(s,lit,render(renderer,overlap));
    check(render(renderer,unshadowed)==lit,"shadow stencil leaked into next frame");
    // A second unshadowed light must illuminate the first light's shadow.
    auto two=s;auto blue=s.lights[0];blue.flags=0;blue.color=0xff0000ff;two.lights.push_back(blue);
    auto second=s;second.lights={blue};const auto blue_only=render(renderer,second);unshadowed=two;unshadowed.lights[0].flags=0;
    check_receiver(two,render(renderer,unshadowed),render(renderer,two),&blue_only);
  }
  }
}
void coplanar_cap() {
  FrameFixture fixture;auto s=scene(fixture);wz3::FrameRenderer renderer(fixture.p,fixture.g);
  // Isolate the finite shadow cap: the caster writes only stencil, while an
  // unlit LIGHT pass reveals stencil-zero pixels on the receiver.
  s.materials[0].passes={s.materials[0].passes.back()};
  auto& light=s.materials[1].passes[1].shader.words;light[0]=0x300280;light[1]=light[2]=0;light[9]=0x10;light[28]=0xffffffff;light[23]=8;
  s.lights[0].position={0,0,6};
  for(float z:{2.49f,2.5f,2.51f}) {
    auto receiver=wall();for(auto& v:receiver->vertices)v.position[2]=z;s.instances[1].mesh=receiver;
    const auto pixels=render(renderer,s);const int expected=z<=2.5f ? 32 : 255;
    check(std::abs(int(pixels[(64*128+64)*4])-expected)<=1,"shadow cap must fail depth at equality (archived D3DCMP_LESS)");
  }
}
void animated() {
  FrameFixture fixture;auto s=scene(fixture);
  const auto cube=fixture.add(0x102,{1,1,1,0,f(1),f(1),f(1),0,0,0,0,0,0});
  std::vector<std::uint32_t> p(21);p[3]=f(.15f);p[4]=f(.2f);p[6]=f(-.1f);p[8]=f(.2f);p[9]=f(.3f);p[10]=f(.1f);
  const auto explode=fixture.add(0x134,p,{cube}),bound=fixture.add(0x110,{0,0},{explode},{0});
  const auto mesh=wz3::evaluate_mesh(fixture.p,fixture.g,bound);s.instances[0].mesh=mesh;
  wz3::FrameRenderer renderer(fixture.p,fixture.g),reference(fixture.p,fixture.g);
  std::vector<std::uint8_t> previous;unsigned changed=0,checks=0;wz3::FrameRenderer::Stats warm;
  for(float time:{0.f,.2f,.7f,1.f,.2f,0.f})for(bool second:{false,true}) {
    auto scene=s;scene.instances[0].time=time;
    if(second){auto other=scene.instances[0];other.time=time+.4f;other.world[12]=1.5f;scene.instances.push_back(other);}
    for(const auto& job:wz3::scene_draw_jobs(scene,128,128).jobs)if(((job.sort_key>>16)&15)==3)check(job.shadow_zfail,"animated mesh must use archived unbounded shadow box");
    const auto actual=render(renderer,scene);
    // Bake each pose as ordinary static input, preserving the posed shadow
    // plane path. This checks time delivery, repeated frames, multiple poses
    // sharing one source, and GPU buffer updates against explicit geometry.
    for(auto& instance:scene.instances)if(wz3::mesh_has_animation(*instance.mesh)) {
      const auto pose=wz3::pose_mesh(instance.mesh,instance.time);
      auto baked=std::make_shared<wz3::Mesh>(*pose);
      auto core=std::make_shared<wz3::legacy::minimal::GenMinMesh>(*pose->min_kernel);core->Animation.reset();baked->min_kernel=core;instance.mesh=baked;
    }
    const auto expected=render(reference,scene);
    for(std::size_t i=0;i<actual.size();++i)check(std::abs(int(actual[i])-expected[i])<=2,"animated GPU geometry/shadow differs from baked pose");
    if(!previous.empty())changed+=actual!=previous;previous=actual;
    if(!checks)warm=renderer.stats();else {check(renderer.stats().mesh_uploads==warm.mesh_uploads,"animation allocated another mesh buffer");check(renderer.stats().shadow_uploads==warm.shadow_uploads,"animation allocated another shadow buffer");}
    ++checks;
  }
  check(changed>5 && renderer.stats().mesh_updates>10 && renderer.stats().shadow_updates>10,"animated GPU test did not exercise changing geometry/shadows");
  auto outside=s;outside.instances[0].world[12]=10000;const auto jobs=wz3::scene_draw_jobs(outside,128,128).jobs;
  check(std::any_of(jobs.begin(),jobs.end(),[](const auto& job){return !job.effect && job.instance==0;}),"animated object incorrectly culled");
  renderer.reset_targets();check(render(renderer,s)==render(renderer,s),"animated reset changed repeated frame");
  std::cout<<checks<<" animated mesh/shadow frames match baked poses; buffers stay bounded\n";
}
void instance_shadows() {
  FrameFixture fixture;auto s=scene(fixture);wz3::FrameRenderer renderer(fixture.p,fixture.g);
  auto& passes=s.materials[0].passes;
  for(auto& pass:passes)if(pass.program!=wz3::MaterialProgram::shadow)pass.program=wz3::MaterialProgram::instances;
  // Zero instances suppress the base/light passes, but the shadow program
  // still draws the one prototype. Compare with an explicit shadow-only mesh.
  const auto actual=render(renderer,s);auto reference=s;
  reference.materials[0].passes.erase(reference.materials[0].passes.begin(),reference.materials[0].passes.end()-1);
  check(render(renderer,reference)==actual,"empty instance mesh lost prototype shadow");
  auto lit=s;lit.lights[0].flags=0;check(render(renderer,lit)!=actual,"instance shadow fixture casts no visible shadow");
  auto matrices=std::make_shared<std::vector<wz3::SceneMatrix>>(3,wz3::scene_identity());
  for(auto& m:*matrices)m[12]=100;
  s.instances[0].particle_matrices=matrices;
  check(render(renderer,s)==actual,"instance transforms must not move or duplicate prototype shadow");
  s.instances[0].world[12]=100;
  const auto jobs=wz3::scene_draw_jobs(s,128,128).jobs;
  check(std::none_of(jobs.begin(),jobs.end(),[](const auto& j){return j.instance==0 && ((j.sort_key>>16)&15)==3;}),"instance flag must not enlarge shadow culling bounds");
  std::cout<<"Instance materials retain one prototype shadow, including empty groups and ordinary shadow culling\n";
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::FramePlayer player(p,g);wz3::FrameRenderer renderer(p,g);
  for(float beat:{56.f,72.f,96.f,120.f,144.f,184.f,216.f,448.5f,480.f,511.5f}) {
    auto frame=player.frame(g.names.at("root"),std::int32_t(beat*65536),320,240);const auto shaded=renderer.render(frame);unsigned jobs=0;
    for(auto& command:frame.commands)if(auto* s=std::get_if<wz3::SceneFrame>(&command.draw)){for(const auto& j:wz3::scene_draw_jobs(*s,command.target.width,command.target.height).jobs)jobs+=((j.sort_key>>16)&15)==3;for(auto& l:s->lights)l.flags&=~2u;}
    const auto lit=renderer.render(frame);unsigned different=0;for(std::size_t i=0;i<shaded.size();i+=4)different+=shaded[i]!=lit[i] || shaded[i+1]!=lit[i+1] || shaded[i+2]!=lit[i+2];
    check(jobs>0,"Theta sample has no shadow jobs");if(beat==56 || beat==72 || beat==216)check(different>50,"Theta shadows changed no visible pixels");
    std::cout<<"Theta beat "<<beat<<": "<<jobs<<" shadow jobs, "<<different<<" affected pixels\n";
  }
}
}
int main(int argc,char** argv){try{controlled();coplanar_cap();animated();instance_shadows();if(argc>1)theta(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
