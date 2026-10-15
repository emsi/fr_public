// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/scene_draw.hpp"
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
int main(int argc,char** argv){try{controlled();coplanar_cap();if(argc>1)theta(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
