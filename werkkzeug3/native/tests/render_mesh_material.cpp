// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <iostream>
#include <algorithm>
namespace {
void pixel(const std::vector<std::uint8_t>& data,unsigned x,unsigned y,std::array<unsigned,3> expected) {
  for(unsigned c=0;c<3;++c)if(std::abs(int(data[(y*64+x)*4+c])-int(expected[c]))>1)
    throw std::runtime_error("mesh pixel at "+std::to_string(x)+","+std::to_string(y)+" channel "+std::to_string(c)+": expected "+std::to_string(expected[c])+", got "+std::to_string(data[(y*64+x)*4+c]));
}
wz3::Material material(std::uint32_t color,unsigned flags=0x300) {
  wz3::Material m;wz3::MaterialPass pass;auto& w=pass.shader.words;w[0]=flags;w[9]=0x10;w[28]=color;w[23]=8;
  for(unsigned i=0;i<4;++i)w[24+i]=f(1);w[34]=w[35]=w[36]=w[43]=w[44]=f(1);m.passes.push_back(pass);return m;
}
std::shared_ptr<wz3::Mesh> square(std::size_t material,float z=2,float left=-1,float right=1) {
  auto mesh=std::make_shared<wz3::Mesh>();
  mesh->vertices={{{left,1,z},{0,0,0}},{{right,1,z},{1,0,0}},{{right,-1,z},{1,1,0}},{{left,-1,z},{0,1,0}}};
  wz3::MeshFace face;face.vertices={0,1,2,3};face.material=material;mesh->faces.push_back(face);return mesh;
}
wz3::DemoFrame frame(wz3::SceneFrame scene) {
  wz3::DemoFrame f;f.width=f.height=64;f.surfaces[0]={64,64};scene.viewport_flags=3;f.commands.push_back({0,{0,64,64},std::move(scene)});return f;
}
void binding_and_cache() {
  FrameFixture x;wz3::FrameRenderer renderer(x.p,x.g);wz3::SceneFrame scene;
  auto mesh=square(10,2,-1,0),right=square(20,2,0,1);mesh->vertices.insert(mesh->vertices.end(),right->vertices.begin(),right->vertices.end());
  auto face=right->faces[0];for(auto& v:face.vertices)v+=4;mesh->faces.push_back(face);
  scene.instances={{0,mesh}};scene.materials[10]=material(0xffff0000);scene.materials[20]=material(0xff0000ff);
  const auto first=renderer.render(frame(scene));pixel(first,24,32,{255,0,0});pixel(first,40,32,{0,0,255});pixel(first,3,3,{0,0,0});
  // Retain geometry, replace the immutable per-frame material snapshot and SRT.
  scene.materials[10].passes[0].shader.words[28]=0xff00ff00;scene.instances[0].world[12]=.5f;
  const auto next=renderer.render(frame(scene));pixel(next,22,32,{0,0,0});pixel(next,32,32,{0,255,0});pixel(next,48,32,{0,0,255});
  pixel(first,24,32,{255,0,0});
}
void depth_culling_and_color() {
  FrameFixture x;wz3::FrameRenderer renderer(x.p,x.g);wz3::SceneFrame scene;scene.materials[1]=material(0xffff0000);scene.materials[2]=material(0xff0000ff);
  auto front=square(1),back=square(2,3);scene.instances={{0,front},{0,back}};
  pixel(renderer.render(frame(scene)),32,32,{255,0,0});
  scene.materials[1].passes[0].shader.words[0]|=4;pixel(renderer.render(frame(scene)),32,32,{0,0,255});
  scene.materials[1].passes[0].shader.words[0]|=2;pixel(renderer.render(frame(scene)),32,32,{255,0,0});
  scene.materials[1].passes[0].shader.words[0]=0x320;pixel(renderer.render(frame(scene)),32,32,{0,0,0}); // depth-only front occludes back
  auto colored=square(1);for(auto& v:colored->vertices)v.color={.25f,.5f,1,1};scene.instances={{0,colored}};
  auto& w=scene.materials[1].passes[0].shader.words;w[0]=0x300;w[9]=0;w[19]=0x10;
  pixel(renderer.render(frame(scene)),32,32,{64,128,255});
}
void mesh_effect_order() {
  FrameFixture x;wz3::FrameRenderer renderer(x.p,x.g);wz3::SceneFrame scene;
  scene.materials[1]=material(0xffff0000,0);scene.materials[1].passes[0].usage=wz3::MaterialUsage::base;
  scene.materials[2]=material(0x800000ff,0x1000);scene.instances={{0,square(1)},{0,square(2)}};
  wz3::SceneEffectDraw effect;effect.sort_key=7u<<16;effect.shader=material(0x8000ff00,0x1000).passes[0].shader;
  effect.vertices={{{-1,1,2},{0,0},0xffffffff},{{1,1,2},{1,0},0xffffffff},{{1,-1,2},{1,1},0xffffffff},{{-1,-1,2},{0,1},0xffffffff}};scene.effects={effect};
  pixel(renderer.render(frame(scene)),32,32,{63,64,128});
  // Moving the mesh pass after the effect overrides its usual base-phase order.
  scene.materials[1].passes[0].render_pass=1;pixel(renderer.render(frame(scene)),32,32,{255,0,0});
}
void target_texture() {
  FrameFixture x;const auto bitmap=x.add(0x26,{4,4,1});
  const auto red=x.layer(x.material(0xffff0000),4,{0,0,1,.5f}),blue=x.layer(x.material(0xff0000ff),4,{0,.5f,1,1});
  const auto select=x.add(0xea,{}, {red,blue}),rt=x.add(0xeb,{}, {select},{bitmap});
  const auto root=x.add(0x0d,{}, {rt});wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);
  auto output=player.frame(root,0,64,64);wz3::SceneFrame scene;scene.viewport_flags=3;scene.instances={{0,square(1)}};scene.materials[1]=material(0xffffffff);
  auto& shader=scene.materials[1].passes[0].shader;shader.words[9]=0;shader.words[11]=0x10;shader.words[4]=0x101;shader.textures[0]=bitmap;
  output.commands.push_back({0,{0,64,64},scene});auto pixels=renderer.render(output);pixel(pixels,32,20,{255,0,0});pixel(pixels,32,44,{0,0,255});
  // Scaling the saved UVs changes which half of the target the mesh samples.
  auto& rendered=std::get<wz3::SceneFrame>(output.commands.back().draw);auto& sh=rendered.materials[1].passes[0].shader;
  sh.words[4]|=0x1000;sh.words[24]=f(.4f);pixels=renderer.render(output);pixel(pixels,32,44,{255,0,0});
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::MaterialRuntime runtime(p,g);wz3::FrameRenderer renderer(p,g);
  for(const char* name:{"mesh.LineCircle","mesh.FireGradientCircle","mesh.fireball","mesh.fireball2","mesh.fireballBlackBody",
                       "mesh.highway2","mesh.citybridge","mesh.cityblock","mesh.bridge2","mesh.citycomp"}) {
    const auto mesh=wz3::evaluate_mesh(p,g,g.names.at(name));wz3::SceneFrame scene;scene.instances={{0,mesh}};
    for(const auto& face:mesh->faces)if(face.material && !scene.materials.contains(*face.material))scene.materials.emplace(*face.material,runtime.snapshot(*face.material));
    const auto bounds=wz3::mesh_bounds(*mesh);float extent=0;
    for(unsigned c=0;c<3;++c){scene.camera.world[12+c]=(bounds.min[c]+bounds.max[c])*.5f;extent=std::max(extent,bounds.max[c]-bounds.min[c]);}
    scene.camera.world[14]-=std::max(extent*1.5f,1.f);auto sample=frame(scene);sample.width=sample.height=384;sample.surfaces[0]={384,384};sample.commands[0].target={0,384,384};
    const auto pixels=renderer.render(sample);unsigned visible=0;for(std::size_t i=0;i<pixels.size();i+=4)visible+=pixels[i] || pixels[i+1] || pixels[i+2];
    check(visible>20,"saved Theta mesh material rendered no visible pixels");std::cout<<name<<": "<<visible<<" visible pixels through saved materials\n";
  }
}
}
int main(int argc,char** argv) {
  try {binding_and_cache();depth_culling_and_color();mesh_effect_order();target_texture();if(argc>1)theta(argv[1]);std::cout<<"Saved mesh material bindings, camera/SRT, depth, culling, colors, blending and target UVs passed\n";}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
