// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/material20.hpp"
#include "wz3/lighting.hpp"
#include <algorithm>
#include <iostream>

namespace {
std::vector<std::uint32_t> parameters(unsigned flags=0) {
  std::vector<std::uint32_t> p(42);p[0]=flags;p[1]=p[2]=0xffffffff;p[3]=f(16);
  for(unsigned i:{12u,13u,14u,15u,20u,21u,22u,23u,24u,25u,26u,33u,34u,39u,40u})p[i]=f(1);
  return p;
}
wz3::Material material(FrameFixture& fixture,std::vector<std::uint32_t> p=parameters(),std::vector<std::optional<std::size_t>> textures={}) {
  textures.resize(7);const auto id=fixture.add(0xd3,std::move(p),{},std::move(textures));
  return wz3::MaterialRuntime(fixture.p,fixture.g).snapshot(id);
}
std::shared_ptr<wz3::Mesh> square(unsigned material=0,float z=2,float half=1) {
  auto m=std::make_shared<wz3::Mesh>();
  m->vertices={{{-half,half,z},{0,0,0}},{{half,half,z},{1,0,0}},{{half,-half,z},{1,1,0}},{{-half,-half,z},{0,1,0}}};
  for(auto& v:m->vertices)v.tangent={1,0,0};wz3::MeshFace face;face.vertices={0,1,2,3};face.material=material;m->faces={face};return m;
}
wz3::SceneFrame scene(wz3::Material m) {
  wz3::SceneFrame s;s.viewport_flags=3;s.instances={{0,square()}};s.materials[0]=std::move(m);
  s.lights={{0,{0,0,0},{0,0,-1},4,0xffffffff,1,1e6f}};return s;
}
wz3::DemoFrame frame(wz3::SceneFrame scene,unsigned size=64) {
  wz3::DemoFrame f;f.width=f.height=size;f.surfaces[0]={size,size};f.commands.push_back({0,{0,size,size},std::move(scene)});return f;
}
void pixel(const std::vector<std::uint8_t>& image,unsigned width,unsigned x,unsigned y,std::array<int,4> expected,int tolerance=2) {
  for(unsigned c=0;c<4;++c)if(std::abs(int(image[(y*width+x)*4+c])-expected[c])>tolerance)
    throw std::runtime_error("Material20 pixel "+std::to_string(x)+","+std::to_string(y)+" channel "+std::to_string(c)+": expected "+std::to_string(expected[c])+", got "+std::to_string(image[(y*width+x)*4+c]));
}
void center(const std::vector<std::uint8_t>& p,std::array<int,4> c,int tolerance=2){pixel(p,64,32,32,c,tolerance);}
void update(wz3::Material& m,unsigned index,std::uint32_t value,bool recompile=false) {
  for(auto& p:m.passes)if(p.shader.material20){p.shader.material20->parameters[index]=value;if(recompile)p.shader.material20->compiled_parameters[index]=value;}
}
void diffuse_ambient_blend() {
  FrameFixture fixture;auto p=parameters();p[1]=0xff804020;auto s=scene(material(fixture,p));wz3::FrameRenderer renderer(fixture.p,fixture.g);
  center(renderer.render(frame(s)),{128,64,32,0});
  s.ambient=0xff204080;center(renderer.render(frame(s)),{160,128,160,0});
  s.lights[0].amplify=.5f;center(renderer.render(frame(s)),{96,96,144,0});
  s.lights.push_back(s.lights[0]);center(renderer.render(frame(s)),{160,128,160,0});
  // Smooth additive blending is per channel, and the prelight copy stays
  // unchanged through all lights and ambient multiplication.
  update(s.materials[0],0,4,true);center(renderer.render(frame(s)),{130,109,143,0},3);
  // Compile-time blending survives animation of the raw flag word.
  update(s.materials[0],0,0);center(renderer.render(frame(s)),{130,109,143,0},3);
  s.lights.resize(1);s.lights[0].flags=0;s.lights[0].range=4;s.lights[0].amplify=1;s.ambient=0;
  update(s.materials[0],0,0,true);center(renderer.render(frame(s)),{96,48,24,64},3);
  s.lights[0].range=2;center(renderer.render(frame(s)),{0,0,0,255});
  // Culling and depth equality: the rear surface must never replace the
  // nearer albedo even if the instance order is reversed.
  s=scene(material(fixture,parameters()));s.materials[1]=material(fixture,p);s.instances.push_back({0,square(1,3)});
  center(renderer.render(frame(s)),{255,255,255,0});std::reverse(s.instances.begin(),s.instances.end());center(renderer.render(frame(s)),{255,255,255,0});
  auto reversed=square();std::reverse(reversed->faces[0].vertices.begin(),reversed->faces[0].vertices.end());s.instances={{0,reversed}};
  center(renderer.render(frame(s)),{0,0,0,255});
}
void specular_bump_color() {
  FrameFixture fixture;const auto flat=fixture.add(0x21,{2,2,0xc0ff8080}),bump=fixture.add(0x25,{5,0,0},{flat});
  auto p=parameters(0xa1);p[1]=0;p[2]=0xff804020;
  auto s=scene(material(fixture,p,{std::nullopt,std::nullopt,std::nullopt,std::nullopt,bump}));wz3::FrameRenderer renderer(fixture.p,fixture.g);
  const auto t=wz3::evaluate_texture(fixture.p,fixture.g,bump);const auto b=wz3::signed_normal_levels(*t)[0].pixels;
  const auto mask=b[3]/127.f;center(renderer.render(frame(s)),{int(128*mask),int(64*mask),int(32*mask),0});
  update(s.materials[0],0,0x21,true);center(renderer.render(frame(s)),{128,64,32,0});
  update(s.materials[0],0,0x61,true);center(renderer.render(frame(s)),{255,128,64,0},3);
  // Specular exponent is also doubled by runtime intensity bit 6.
  const auto l=wz3::material20_lighting(*s.materials[0].passes.back().shader.material20,wz3::scene_identity(),s.camera,s.lights[0]);near(l.specular[3],32,"2x Material20 exponent");
  update(s.materials[0],0,0x29,true);auto colored=square();for(auto& v:colored->vertices)v.color={.25f,.5f,1,.5f};s.instances={{0,colored}};
  center(renderer.render(frame(s)),{32,32,32,0});
  // Detail-only bump is the first sampled map, including its specularity alpha.
  s=scene(material(fixture,p,{std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt,bump}));
  center(renderer.render(frame(s)),{int(128*mask),int(64*mask),int(32*mask),0});
  // Two maps are summed and normalized even without the renormalize flag.
  p[0]=0;p[1]=0xff804020;p[2]=0;s=scene(material(fixture,p,{std::nullopt,std::nullopt,std::nullopt,std::nullopt,bump,bump}));
  center(renderer.render(frame(s)),{128,64,32,0});
}
void alpha_details_correction() {
  FrameFixture fixture;const auto main=fixture.add(0x21,{2,2,0xc0804020}),detail=fixture.add(0x21,{2,2,0xff4080c0});
  const auto low=fixture.add(0x21,{2,2,0x80ffffff}),high=fixture.add(0x21,{2,2,0x81ffffff});
  auto p=parameters();p[1]=0;auto s=scene(material(fixture,p,{main}));s.ambient=0xffffffff;wz3::FrameRenderer renderer(fixture.p,fixture.g);
  center(renderer.render(frame(s)),{128,64,32,192});
  const std::array<std::array<int,4>,5> expected={{{32,32,24,192},{64,64,48,192},{129,129,96,192},{192,192,224,192},{160,160,200,192}}};
  for(unsigned mode=0;mode<5;++mode){p[9]=mode<<16;s.materials[0]=material(fixture,p,{main,detail});center(renderer.render(frame(s)),expected[mode]);}
  p[9]=0;s.materials[0]=material(fixture,p,{main,std::nullopt,std::nullopt,low});center(renderer.render(frame(s)),{0,0,0,255});
  s.materials[0]=material(fixture,p,{main,std::nullopt,std::nullopt,high});center(renderer.render(frame(s)),{128,64,32,192});
  update(s.materials[0],0,0x100,true);update(s.materials[0],4,f(.51f));center(renderer.render(frame(s)),{0,0,0,255});
  update(s.materials[0],4,f(.49f));center(renderer.render(frame(s)),{128,64,32,192});
  p[0]=0x200;p[39]=0;s.materials[0]=material(fixture,p,{main});center(renderer.render(frame(s)),{81,81,81,192});
  update(s.materials[0],40,f(.5f));update(s.materials[0],41,f(.1f));center(renderer.render(frame(s)),{130,130,130,192});
}
wz3::FrameQuad solid(std::uint32_t color,wz3::FrameRect rectangle) {
  wz3::FrameQuad q;q.rectangle=rectangle;q.shader.words[0]=0x20002;q.shader.words[9]=0x10;q.shader.words[28]=color;q.shader.words[23]=8;return q;
}
wz3::DemoFrame pattern(wz3::SceneFrame scene,std::size_t bitmap,unsigned size=64) {
  auto out=frame(std::move(scene),size);const auto target=13+bitmap;out.surfaces[target]={64,64};
  std::vector<wz3::FrameCommand> prefix;
  prefix.push_back({0,{target,64,64},solid(0xffff0000,{0,0,.5f,.5f})});
  prefix.push_back({0,{target,64,64},solid(0xff00ff00,{.5f,0,1,.5f})});
  prefix.push_back({0,{target,64,64},solid(0xff0000ff,{0,.5f,.5f,1})});
  prefix.push_back({0,{target,64,64},solid(0xffffffff,{.5f,.5f,1,1})});
  out.commands.insert(out.commands.begin(),prefix.begin(),prefix.end());return out;
}
void capture_uv_and_environment() {
  FrameFixture fixture;const auto bitmap=fixture.add(0x26,{6,6,1});auto p=parameters();p[8]=0x100;
  auto s=scene(material(fixture,p,{bitmap}));wz3::FrameRenderer renderer(fixture.p,fixture.g);
  auto image=renderer.render(pattern(s,bitmap));pixel(image,64,22,22,{255,0,0,0});pixel(image,64,42,22,{0,255,0,0});pixel(image,64,22,42,{0,0,255,0});pixel(image,64,42,42,{255,255,255,0});
  // Compare the light-only result with the ambient-only albedo at every pixel,
  // including the red/green and blue/white seams. Detects a one-pixel capture
  // shift, vertical inversion, or feedback from the ambient pass.
  auto ambient=s;ambient.ambient=0xffffffff;update(ambient.materials[0],1,0);
  const auto a=renderer.render(pattern(ambient,bitmap));
  for(unsigned y=18;y<46;++y)for(unsigned x=18;x<46;++x)for(unsigned c=0;c<3;++c)
    check(std::abs(int(image[(y*64+x)*4+c])-int(a[(y*64+x)*4+c]))<=1,"projected albedo lookup moved a pixel");
  // A smaller viewport reuses the larger capture, keeping top-left placement.
  image=renderer.render(pattern(s,bitmap,32));pixel(image,32,10,10,{255,0,0,0});pixel(image,32,21,21,{255,255,255,0});
  image=renderer.render(pattern(s,bitmap,96));pixel(image,96,32,32,{255,0,0,0});pixel(image,96,64,64,{255,255,255,0});
  renderer.reset_targets();image=renderer.render(pattern(s,bitmap));pixel(image,64,22,42,{0,0,255,0});
  // Translation matrices are supplied at runtime; changing sampler/mode bits
  // after compilation does not select a different UV program.
  update(s.materials[0],8,0x2100,true);update(s.materials[0],30,f(.5f));
  image=renderer.render(pattern(s,bitmap));pixel(image,64,22,22,{0,255,0,0});
  update(s.materials[0],8,0);image=renderer.render(pattern(s,bitmap));pixel(image,64,22,22,{0,255,0,0});
  update(s.materials[0],8,0x3100,true);update(s.materials[0],35,f(3.14159265358979323846f/2));update(s.materials[0],37,f(1));
  image=renderer.render(pattern(s,bitmap));pixel(image,64,22,22,{0,0,255,0});pixel(image,64,42,22,{255,0,0,0});
  // Environment mapping samples normal.xy, not the mesh's UVs. A tilted
  // plane keeps both normal and reflected directions away from seam ties.
  p[1]=0;p[18]=0x100;auto mesh=square();
  for(auto& v:mesh->vertices){v.position[2]+=.25f*(v.position[0]+v.position[1]);v.tangent={.9701425f,0,.2425356f};}
  s=scene(material(fixture,p,{std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt,bitmap}));s.instances={{0,mesh}};
  center(renderer.render(pattern(s,bitmap)),{0,255,0,255});
  update(s.materials[0],18,0x10100,true);center(renderer.render(pattern(s,bitmap)),{0,0,255,255});
  const auto normal=fixture.add(0x21,{2,2,0xc080ff80}),bump=fixture.add(0x25,{5,0,0},{normal});
  p[18]=0x20100;p[0]=0x80;s.materials[0]=material(fixture,p,{std::nullopt,std::nullopt,std::nullopt,std::nullopt,bump,std::nullopt,bitmap});
  center(renderer.render(pattern(s,bitmap)),{128,128,128,255},3);
  // Material20's insert takes precedence over a Material11 insert in the same
  // pass: the latter's destination-alpha specular add would turn this white.
  s.materials[0].material11_insert=true;center(renderer.render(pattern(s,bitmap)),{128,128,128,255},3);
}
void all_debris_shaders(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::MaterialRuntime runtime(p,g);
  FrameFixture fixture;const auto flat=fixture.add(0x21,{2,2,0xff808080}),normal=fixture.add(0x21,{2,2,0xffffffff}),bump=fixture.add(0x25,{5,0,0},{normal});
  wz3::FrameRenderer renderer(fixture.p,fixture.g);
  unsigned shaders=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0xd3) {
    auto m=runtime.snapshot(id);
    for(auto& pass:m.passes)if(pass.shader.material20) {
      auto& s=*pass.shader.material20;for(unsigned i=0;i<7;++i)if(s.textures[i])s.textures[i]=(i==4 || i==5) ? bump : flat;++shaders;
    }
    renderer.render(frame(scene(m)));
  }
  check(shaders>400,"missing Debris Material20 shader variants");
  std::cout<<shaders<<" saved Debris Material20 stages compiled/rendered with controlled replacement textures ("<<renderer.stats().program_compilations<<" unique programs)\n";
}
}
int main(int argc,char** argv) {
  try {diffuse_ambient_blend();specular_bump_color();alpha_details_correction();capture_uv_and_environment();if(argc>1)all_debris_shaders(argv[1]);
    std::cout<<"Material20 GPU passes, capture, alpha, bump/specular, color correction, blending and UV transforms passed\n";}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
