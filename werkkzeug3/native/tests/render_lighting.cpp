// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <iostream>
namespace {
wz3::MaterialPass solid(std::uint32_t color,unsigned flags,wz3::MaterialUsage usage) {
  wz3::MaterialPass p;p.usage=usage;auto& w=p.shader.words;w[0]=flags;w[9]=0x10;w[28]=color;w[23]=8;
  for(unsigned i=0;i<4;++i)w[24+i]=f(1);w[34]=w[35]=w[36]=w[43]=w[44]=f(1);return p;
}
wz3::MaterialPass light(std::uint32_t color=0xffff8040) {
  auto p=solid(color,0x300,wz3::MaterialUsage::light);p.shader.words[2]=5;p.shader.words[1]=0x40;p.shader.words[32]=f(8);return p;
}
wz3::SceneFrame scene() {
  wz3::SceneFrame s;s.viewport_flags=3;auto mesh=std::make_shared<wz3::Mesh>();
  mesh->vertices={{{-1,1,2},{0,0,0}},{{1,1,2},{1,0,0}},{{1,-1,2},{1,1,0}},{{-1,-1,2},{0,1,0}}};for(auto& v:mesh->vertices)v.tangent={1,0,0};
  wz3::MeshFace face;face.vertices={0,1,2,3};face.material=0;mesh->faces={face};s.instances={{0,mesh}};s.materials[0].passes={light()};
  s.lights={{1,{0,0,0},{0,0,-1},4,0xffffffff,1,1e6f}};return s;
}
std::vector<std::uint8_t> render(wz3::FrameRenderer& renderer,wz3::SceneFrame s) {
  wz3::DemoFrame frame;frame.width=frame.height=64;frame.surfaces[0]={64,64};frame.commands.push_back({0,{0,64,64},std::move(s)});return renderer.render(frame);
}
void center(const std::vector<std::uint8_t>& pixels,std::array<int,4> expected,int tolerance=2) {
  for(unsigned c=0;c<4;++c){const int got=pixels[(32*64+32)*4+c];if(std::abs(got-expected[c])>tolerance)throw std::runtime_error("light channel "+std::to_string(c)+": expected "+std::to_string(expected[c])+", got "+std::to_string(got));}
}
void diffuse_and_range() {
  FrameFixture fixture;wz3::FrameRenderer renderer(fixture.p,fixture.g);auto s=scene();
  center(render(renderer,s),{254,127,64,0});
  s.materials[0].passes[0].shader.words[0]|=1;center(render(renderer,s),{0,0,0,255});s.materials[0].passes[0].shader.words[0]&=~1u;
  s.lights[0].amplify=.5f;center(render(renderer,s),{127,64,32,0});
  s.lights[0].amplify=1;s.lights[0].direction={1,0,0};center(render(renderer,s),{1,1,0,0});
  s.lights[0].flags=0;s.lights[0].range=4;center(render(renderer,s),{192,96,48,0},4);
  s.lights[0].range=2;center(render(renderer,s),{0,0,0,0});
  // Two selected lights generate two additive jobs, rather than overwriting one another.
  s=scene();s.materials[0].passes[0].shader.words[0]=0x2300;s.lights[0].amplify=.25f;s.lights.push_back(s.lights[0]);center(render(renderer,s),{128,64,32,255});
}
void signed_bump() {
  FrameFixture fixture;const auto flat=fixture.add(0x21,{2,2,0xffff8080}),format=fixture.add(0x25,{5,0,0},{flat});
  // Format is opcode 0x25 in the saved bitmap registry.
  wz3::FrameRenderer renderer(fixture.p,fixture.g);auto s=scene();auto& shader=s.materials[0].passes[0].shader;shader.textures[1]=format;shader.words[5]=2;
  // Flat 0xff8080 expands the neutral bytes to 16448; normalization
  // and integer truncation store 126/127 in the facing channel.
  center(render(renderer,s),{252,126,63,0});
  auto t=scene();t.materials[0].passes[0].shader.textures[1]=format;t.materials[0].passes[0].shader.words[5]=2;t.lights[0].direction={1,0,0};center(render(renderer,t),{1,1,0,0},3);
}
void insert_specular() {
  FrameFixture fixture;wz3::FrameRenderer renderer(fixture.p,fixture.g);auto s=scene();auto& m=s.materials[0];m.material11_insert=true;
  auto base=solid(0xff000000,0x300,wz3::MaterialUsage::base);base.shader.words[23]=13;
  auto lamp=light(0x80800000);lamp.shader.words[0]=0x302280;lamp.shader.words[1]=0; // depth-equal, additive, stencil-zero
  auto post=solid(0xff808080,0x4280,wz3::MaterialUsage::postlight);post.shader.words[23]=12;
  m.passes={base,lamp,post};
  center(render(renderer,s),{255,127,127,127},4);
  // A specular map without a bump texture reads the cube alpha (zero).
  m.passes[1].shader.words[1]=0x80;center(render(renderer,s),{128,0,0,0},3);
  // Diffuse-only writes zero alpha, so the final specular insert adds nothing.
  m.passes[1].shader.words[1]=0x40;center(render(renderer,s),{128,0,0,0},3);
  // A second frame must clear the previous specular alpha after the base pass.
  m.passes[1].shader.words[1]=0;center(render(renderer,s),{255,127,127,127},4);
  m.passes[1].shader.words[1]=0x40;center(render(renderer,s),{128,0,0,0},3);
}
void released_effect_order() {
  FrameFixture fixture;fixture.p.runtime_graph=true;wz3::FrameRenderer renderer(fixture.p,fixture.g);auto s=scene();
  auto base=solid(0xff000000,0x300,wz3::MaterialUsage::base);base.shader.words[23]=13;
  auto lamp=light(0x00800000);lamp.shader.words[0]=0x302280;
  auto post=solid(0xff808080,0x4280,wz3::MaterialUsage::postlight);post.shader.words[23]=12;
  s.materials[0].material11_insert=true;s.materials[0].passes={base,lamp,post};
  s.lights[0].amplify=.5f;s.lights.push_back(s.lights[0]);
  wz3::SceneEffectDraw effect;effect.sort_key=7u<<16;
  effect.shader=solid(0xff000020,0x2002,wz3::MaterialUsage::other).shader;
  effect.vertices={{{-.75f,.75f,1},{0,0},0xffffffff},{{.75f,.75f,1},{1,0},0xffffffff},
                   {{.75f,-.75f,1},{1,1},0xffffffff},{{-.75f,-.75f,1},{0,1},0xffffffff}};
  s.effects={effect};
  // The original completes every light and the postlight/specular phase before
  // ordinary effects, even when their material pass numbers are lower. Drawing
  // this alpha-one effect early turns its rectangle white in the specular add.
  for(unsigned i=0;i<2;++i)center(render(renderer,s),{128,0,32,255},3);
  auto& passes=s.materials[0].passes;passes[0].render_pass=2;passes[1].render_pass=1;passes[2].render_pass=3;
  center(render(renderer,s),{128,0,32,255},3);
  s.viewport_flags=0x13;center(render(renderer,s),{0,0,32,255}); // simple: other only
  s.viewport_flags=0x23;center(render(renderer,s),{128,0,32,255},3); // no specular
  // Empty lighting phases still clear alpha before adding specular: an unlit
  // scene with an opaque black background must not become a white screen.
  s.viewport_flags=3;s.instances.clear();s.materials.clear();s.lights.clear();
  center(render(renderer,s),{0,0,32,255});
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::FramePlayer player(p,g);wz3::FrameRenderer renderer(p,g);
  for(int beat:{4,204,256,336,384,432}) {
    const auto frame=player.frame(g.names.at("root"),beat*65536,320,240);const auto pixels=renderer.render(frame);unsigned visible=0;
    for(std::size_t i=0;i<pixels.size();i+=4)visible+=pixels[i] || pixels[i+1] || pixels[i+2];check(visible>(beat==4 ? 50u : 1000u),"strict Theta lighting frame is empty");
    std::cout<<"Theta beat "<<beat<<": "<<visible<<" visible pixels with saved lighting/materials\n";
  }
}
}
int main(int argc,char** argv){try{diffuse_and_range();signed_bump();insert_specular();released_effect_order();if(argc>1)theta(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
