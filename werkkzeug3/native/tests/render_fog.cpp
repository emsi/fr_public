// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <iostream>
namespace {
wz3::MaterialShader shader() {
  wz3::MaterialShader s;auto& w=s.words;w[0]=0x340;w[9]=0x10;w[28]=0x80ff0000;w[23]=8;return s;
}
std::shared_ptr<wz3::Mesh> square(float z) {
  auto m=std::make_shared<wz3::Mesh>();m->vertices={{{-10,10,z}},{{10,10,z}},{{10,-10,z}},{{-10,-10,z}}};for(auto& v:m->vertices)v.tangent={1,0,0};
  wz3::MeshFace f;f.vertices={0,1,2,3};f.material=0;m->faces={f};return m;
}
wz3::SceneFrame scene(float z=3) {
  wz3::SceneFrame s;s.viewport_flags=3;s.camera.fog_start=2;s.camera.fog_end=4;s.camera.fog_color=0x000000ff;s.instances={{0,square(z)}};
  wz3::MaterialPass p;p.shader=shader();s.materials[0].passes={p};return s;
}
std::vector<std::uint8_t> render(wz3::FrameRenderer& r,wz3::SceneFrame s) {
  wz3::DemoFrame f;f.width=f.height=64;f.surfaces[0]={64,64};f.commands.push_back({0,{0,64,64},std::move(s)});return r.render(f);
}
void pixel(const std::vector<std::uint8_t>& p,std::array<int,4> expected,unsigned x=32,unsigned y=32) {
  for(unsigned c=0;c<4;++c)if(std::abs(int(p[(y*64+x)*4+c])-expected[c])>2)throw std::runtime_error("fog pixel at "+std::to_string(x)+","+std::to_string(y)+": expected "+std::to_string(expected[c])+", got "+std::to_string(p[(y*64+x)*4+c]));
}
void endpoints_and_alpha() {
  FrameFixture f;wz3::FrameRenderer r(f.p,f.g);
  for(float z:{1.f,2.f,3.f,4.f,5.f}) {const auto factor=std::clamp((4-z)*.5f,0.f,1.f);pixel(render(r,scene(z)),{int(255*factor+.5f),0,int(255*(1-factor)+.5f),128});}
  auto s=scene();s.instances[0].world[14]=1;pixel(render(r,s),{0,0,255,128});s.camera.world[14]=1;pixel(render(r,s),{128,0,128,128});
  s=scene(5);s.materials[0].passes[0].shader.words[0]&=~0x40u;pixel(render(r,s),{255,0,0,128});s.materials[0].passes[0].shader.words[0]|=0x40;s.camera.fog_color=0xff00ff00;pixel(render(r,s),{0,255,0,128});
  s=scene();s.background=0xff00ff00;s.materials[0].passes[0].shader.words[0]|=0x1000;pixel(render(r,s),{64,127,64,191});
  s.materials[0].passes[0].shader.words[0]|=1;pixel(render(r,s),{0,255,0,255}); // Fog must not bypass alpha-test rejection.
}
void vertex_clamping() {
  FrameFixture f;wz3::FrameRenderer r(f.p,f.g);auto s=scene();auto m=std::make_shared<wz3::Mesh>();
  const std::array<wz3::Vec3,3> projected={{{-.8f,-.8f,1},{.8f,-.8f,5},{0,.8f,5}}};
  for(auto p:projected)m->vertices.push_back({{p[0]*p[2]+1.f/64,p[1]*p[2]-1.f/64,p[2]}});
  wz3::MeshFace face;face.count=3;face.vertices={0,1,2,0};face.material=0;m->faces={face};s.instances[0].mesh=m;s.materials[0].passes[0].shader.words[0]|=2;
  const auto pixels=render(r,s);unsigned checked=0;
  for(unsigned y=8;y<56;y+=3)for(unsigned x=8;x<56;x+=3) {
    const float px=2*(x+.5f)/64-1,py=1-2*(y+.5f)/64;
    const float c=(py+.8f)/1.6f,b=(px+.8f-.8f*c)/1.6f,a=1-b-c;if(std::min({a,b,c})<.1f)continue;
    // oFog is clamped to {1,0,0} before perspective interpolation.
    const auto factor=a/(a+b/5+c/5);pixel(pixels,{int(255*factor+.5f),0,int(255*(1-factor)+.5f),128},x,y);++checked;
  }
  check(checked>20,"fog interpolation fixture lacks interior samples");
}
void lighting_and_effects() {
  FrameFixture f;wz3::FrameRenderer r(f.p,f.g);auto s=scene();auto& pass=s.materials[0].passes[0];pass.usage=wz3::MaterialUsage::light;
  pass.shader.words[1]=0x40;pass.shader.words[2]=5;pass.shader.words[28]=0x00ff0000;pass.shader.words[32]=::f(8);s.lights={{0,{0,0,0},{0,0,-1},4,0xffffffff,1,1e6f}};
  pixel(render(r,s),{127,0,128,0});
  s=scene();s.instances.clear();wz3::SceneEffectDraw e;e.sort_key=7u<<16;e.shader=shader();e.vertices={{{-10,10,3}},{{10,10,3}},{{10,-10,3}},{{-10,-10,3}}};s.effects={e};pixel(render(r,s),{128,0,128,128});
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::FramePlayer player(p,g);wz3::FrameRenderer renderer(p,g);
  for(int beat:{290,298,304,306,314,319}) {
    auto frame=player.frame(g.names.at("root"),beat*65536,320,240);const auto fogged=renderer.render(frame);unsigned passes=0;
    for(auto& command:frame.commands)if(auto* s=std::get_if<wz3::SceneFrame>(&command.draw))for(auto& [id,m]:s->materials)for(auto& pass:m.passes){passes+=bool(pass.shader.words[0]&0x40);pass.shader.words[0]&=~0x40u;}
    const auto clear=renderer.render(frame);unsigned different=0;for(std::size_t i=0;i<fogged.size();i+=4)different+=fogged[i]!=clear[i] || fogged[i+1]!=clear[i+1] || fogged[i+2]!=clear[i+2];
    check(passes>0,"Theta fog sample has no fogged material");if(beat==290 || beat==298)check(different>100,"Theta fog changed no visible pixels");std::cout<<"Theta beat "<<beat<<": "<<passes<<" fogged passes, "<<different<<" affected pixels\n";
  }
}
}
int main(int argc,char** argv){try{endpoints_and_alpha();vertex_clamping();lighting_and_effects();if(argc>1)theta(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
