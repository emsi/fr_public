// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <iostream>
namespace {
using Pixels=std::vector<std::uint8_t>;
void pixel(const Pixels& p,unsigned width,unsigned x,unsigned y,std::array<unsigned,3> wanted,unsigned tolerance=1) {
  for(unsigned c=0;c<3;++c) {
    const int got=p[(std::size_t(y)*width+x)*4+c];
    if(std::abs(got-int(wanted[c]))>int(tolerance))throw std::runtime_error("frame pixel expected "+std::to_string(wanted[c])+", got "+std::to_string(got));
  }
}
void layers() {
  FrameFixture x;const auto red=x.layer(x.material(0xffff0000),4);const auto blue=x.material(0x800000ff,0x1000);x.p.operators[blue].parameters[23]=8;
  const auto overlay=x.layer(blue,4,{.25f,.25f,.75f,.75f});const auto root=x.add(0x0d,{}, {red,overlay});
  wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);auto frame=player.frame(root,0,32,16);const auto pixels=renderer.render(frame);
  pixel(pixels,32,0,0,{255,0,0});pixel(pixels,32,31,15,{255,0,0});pixel(pixels,32,16,8,{127,0,128});
  frame.commands.clear();check(renderer.render(frame)==pixels,"renderer must retain screen contents across frames without draws");
}
void released_layer_centers() {
  for(unsigned width:{64u,128u})for(unsigned size:{3u,4u}) {
    FrameFixture x;x.p.runtime_graph=true;
    const auto layer=x.layer(x.material(0xffff0000),size);
    const auto root=x.add(0x0d,{}, {layer});wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);
    auto frame=player.frame(root,0,width,64);
    // Integer centers on top/left edges belong to the layer; bottom/right
    // edges do not. Include clipped, reversed, subpixel and empty rectangles.
    // Size 3 also tests a cropped pool target and the subsequent FXQuad copy.
    const wz3::FrameRect cases[]={{16.25f,16.25f,31.25f,31.25f},{16,16,31,31},
      {16.5f,16.5f,31.5f,31.5f},{16.75f,16.75f,31.75f,31.75f},
      {0,0,float(width),64},{-5,-3,17,19},{31,16,16,31},{16,31,31,16},
      {31,31,16,16},{16.25f,16.25f,16.75f,16.75f},{16.75f,16.75f,17.25f,17.25f},
      {16,16,16,31},{16,16,31,16}};
    for(const auto& r:cases) {
      for(auto& c:frame.commands)if(auto* q=std::get_if<wz3::FrameQuad>(&c.draw);q && q->released_layer)
        q->rectangle={r[0]/width,r[1]/64,r[2]/width,r[3]/64};
      renderer.reset_targets();const auto pixels=renderer.render(frame);
      for(unsigned y=0;y<64;++y)for(unsigned px=0;px<width;++px) {
        const bool covered=px>=std::min(r[0],r[2]) && px<std::max(r[0],r[2]) &&
          y>=std::min(r[1],r[3]) && y<std::max(r[1],r[3]);
        pixel(pixels,width,px,y,{covered ? 255u : 0u,0,0});
      }
    }
  }
}
void released_layer_uv() {
  FrameFixture x;x.p.runtime_graph=true;const auto layer=x.layer(x.material(0xffff0000),4);
  const auto root=x.add(0x0d,{}, {layer});
  wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);
  auto frame=player.frame(root,0,64,64);
  const auto base=std::get<wz3::FrameQuad>(frame.commands.back().draw);frame.commands.clear();frame.surfaces[1]={4,4};
  // Build a known RG gradient in an offscreen surface. Use compensated FXQuad
  // coverage to fill texels, then sample it through the original Layer2D UVs.
  for(unsigned y=0;y<4;++y)for(unsigned px=0;px<4;++px) {
    auto q=base;q.released_layer=false;q.rectangle={px/4.f,y/4.f,(px+1)/4.f,(y+1)/4.f};
    q.shader.words[28]=0xff000000|(px*64<<16)|(y*64<<8);frame.commands.push_back({0,{1,4,4},q});
  }
  auto q=base;q.shader.words[9]=0;q.shader.words[11]=0x10;q.shader.words[4]=0x101;
  q.shader.textures[0]=1;q.textures[0]=wz3::FrameTexture{true,1};
  frame.commands.push_back({0,{0,64,64},q});
  for(float edge:{16.f,16.25f,16.5f,16.75f})for(bool reversed:{false,true}) {
    auto& shown=std::get<wz3::FrameQuad>(frame.commands.back().draw);
    const float end=edge+15;shown.rectangle={edge/64,edge/64,end/64,end/64};
    if(reversed){std::swap(shown.rectangle[0],shown.rectangle[2]);std::swap(shown.rectangle[1],shown.rectangle[3]);}
    renderer.reset_targets();const auto pixels=renderer.render(frame);
    for(unsigned y=0;y<64;++y)for(unsigned px=0;px<64;++px) {
      const bool covered=px>=edge && px<end && y>=edge && y<end;
      const auto value=[&](unsigned v){const float uv=reversed ? (end-v)/15 : (v-edge)/15;
        return unsigned(std::lround(std::clamp(uv*4-.5f,0.f,3.f)*64));};
      pixel(pixels,64,px,y,{covered ? value(px) : 0u,covered ? value(y) : 0u,0},1);
    }
  }
}
void targets() {
  FrameFixture x;const auto bitmap=x.add(0x26,{4,4,1});
  const auto red=x.layer(x.material(0xffff0000),4,{0,0,1,.5f}),blue=x.layer(x.material(0xff0000ff),4,{0,.5f,1,1});
  const auto select=x.add(0xea,{}, {red,blue});const auto rt=x.add(0xeb,{}, {select},{bitmap});
  const auto texture=x.material(0xffffffff);auto& p=x.p.operators[texture].parameters;p[9]=0;p[11]=0x10;p[4]=0x101;x.g.nodes[texture].links={bitmap};
  const auto shown=x.layer(texture,3),root=x.add(0x0d,{}, {rt,shown});
  wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);
  for(unsigned i=0;i<2;++i) {
    const auto frame=player.frame(root,i*65536,32,16);const auto pixels=renderer.render(frame);
    pixel(pixels,32,2,2,{255,0,0});pixel(pixels,32,29,13,{0,0,255});
  }
  // Cropped pool targets must remain correct when the output resolution changes.
  const auto resized=renderer.render(player.frame(root,2*65536,48,32));pixel(resized,48,2,2,{255,0,0});pixel(resized,48,45,29,{0,0,255});
  const auto cached=renderer.stats();renderer.reset_targets();
  pixel(renderer.readback(),48,2,2,{0,0,0});
  // Sample the retained bitmap target without writing it again. A loading pass
  // must leave no old colors in the screen, pool or named render targets.
  auto replay=player.frame(root,3*65536,48,32);const auto last=replay.commands.back();replay.commands={last};
  const auto cleared=renderer.render(replay);pixel(cleared,48,2,2,{0,0,0});pixel(cleared,48,45,29,{0,0,0});
  check(renderer.stats()==cached,"reset targets destroyed cached resources");
  auto& q=std::get<wz3::FrameQuad>(replay.commands[0].draw);q.shader.words[0]|=0x300;renderer.render(replay);
  q.depth=.5f;q.shader.textures={};q.textures={};
  std::fill(q.shader.words.begin()+8,q.shader.words.begin()+21,0);q.shader.words[9]=0x10;q.shader.words[23]=8;q.shader.words[28]=0xff00ff00;
  // Previous Layer2D writes have much nearer depth; this needs a depth reset too.
  pixel(renderer.render(replay),48,20,15,{0,0,0});
  renderer.reset_targets();pixel(renderer.render(replay),48,20,15,{0,255,0});
}
void release_depth() {
  FrameFixture x;x.p.runtime_graph=true;wz3::FrameRenderer renderer(x.p,x.g);
  wz3::DemoFrame frame;frame.width=frame.height=16;frame.surfaces={{0,{16,16}},{1,{12,8}},{2,{4,4}}};
  const auto quad=[](std::uint32_t color,float depth) {
    wz3::FrameQuad q;q.depth=depth;q.shader.words[0]=0x20302;q.shader.words[9]=0x10;q.shader.words[28]=color;
    for(unsigned i=0;i<4;++i)q.shader.words[24+i]=f(1);
    q.shader.words[34]=q.shader.words[35]=q.shader.words[36]=q.shader.words[43]=q.shader.words[44]=f(1);return q;
  };
  auto red=quad(0xffff0000,.25f);red.rectangle={0,0,1,.5f};const auto green=quad(0xff00ff00,.5f),blue=quad(0xff0000ff,.75f);
  // A near draw into a shorter offscreen target occludes the screen at the
  // same TOP-left coordinates, without copying that target's red color.
  frame.commands={{0,{1,12,8},red},{0,{0,16,16},green}};
  auto pixels=renderer.render(frame);pixel(pixels,16,2,2,{0,0,0});pixel(pixels,16,6,2,{0,0,0});
  pixel(pixels,16,2,8,{0,255,0});pixel(pixels,16,14,2,{0,255,0});
  const auto warmed=renderer.stats();
  // The next frame clears depth once before the graph, even if no operator
  // requests a depth clear. This caught the stale pillar view in final Theta.
  frame.commands={{0,{0,16,16},blue}};pixels=renderer.render(frame);
  pixel(pixels,16,2,2,{0,0,255});pixel(pixels,16,6,2,{0,0,255});pixel(pixels,16,2,8,{0,0,255});
  // A clear through a smaller target affects only its shared depth rectangle;
  // pixels outside it survive the round trip through that smaller attachment.
  frame.commands={{0,{1,12,8},red},{0,{2,4,4},wz3::FrameClear{2,0}},{0,{0,16,16},green}};
  pixels=renderer.render(frame);pixel(pixels,16,2,2,{0,255,0});pixel(pixels,16,6,2,{0,0,0});pixel(pixels,16,2,8,{0,255,0});
  // Texture clears cover the full allocation even when its draw viewport is
  // smaller. Screen clears retain their requested viewport rectangle.
  frame.commands={{0,{1,12,8},red},{0,{1,4,4},wz3::FrameClear{2,0}},{0,{0,16,16},green}};
  pixels=renderer.render(frame);pixel(pixels,16,6,2,{0,255,0});
  frame.commands={{0,{1,12,8},red},{0,{0,4,4},wz3::FrameClear{2,0}},{0,{0,16,16},green}};
  pixels=renderer.render(frame);pixel(pixels,16,2,2,{0,255,0});pixel(pixels,16,6,2,{0,0,0});
  // Per-frame initialization must preserve offscreen COLOR history.
  auto shown=quad(0xffffffff,0);shown.shader.words[0]=0x20002;shown.shader.words[9]=0;shown.shader.words[11]=0x10;
  shown.shader.words[4]=0x101;shown.shader.textures[0]=1;shown.textures[0]=wz3::FrameTexture{true,1};
  frame.commands={{0,{0,16,16},shown}};pixels=renderer.render(frame);
  pixel(pixels,16,8,2,{255,0,0});pixel(pixels,16,8,13,{0,0,0});
  check(renderer.stats().surface_allocations==warmed.surface_allocations,"release depth transfer allocated targets during playback");
  renderer.reset_targets();pixel(renderer.render(frame),16,8,2,{0,0,0});
  frame.width=24;frame.height=12;frame.surfaces[0]={24,12};frame.commands={{0,{0,24,12},green}};
  pixels=renderer.render(frame);pixel(pixels,24,2,2,{0,255,0});pixel(pixels,24,23,11,{0,255,0});
  frame.commands.clear();pixels=renderer.render(frame);pixel(pixels,24,2,2,{0,0,0});
}
void blur_and_merge() {
  FrameFixture x;const auto base=x.layer(x.material(0xff111111));
  const auto blur=x.add(0xe3,{5,0,f(1),1,4},{base});const auto root=x.add(0x0d,{}, {blur});
  wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);
  const auto frame=player.frame(root,0,32,16);check(frame.commands.size()==4,"zero-radius blur still executes one two-pass stage");
  const auto pixels=renderer.render(frame);
  // 17 * (64/255) * 2 rounds to 9 in the first RGBA8 draw; the additive
  // second draw rounds 9 + 8.533... to 18. A single 4-tap sum gives 17.
  pixel(pixels,32,16,8,{18,18,18},0);
}
void crashzoom_and_merge() {
  FrameFixture x;const auto a=x.layer(x.material(0xff303030)),b=x.layer(x.material(0xff606060));
  const auto merge=x.add(0xe7,{3,1,128,0x404040},{a,b}),zoom=x.add(0xe4,{5,2,f(1.1f),f(1),f(.5f),f(.5f)},{merge});
  const auto root=x.add(0x0d,{}, {zoom});wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);
  const auto pixels=renderer.render(player.frame(root,0,32,16));pixel(pixels,32,16,8,{126,126,126});
}
void scene_opt_in() {
  FrameFixture x;const auto root=x.add(0x0d,{}, {x.viewport()});wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);
  auto frame=player.frame(root,0,32,32);bool rejected=false;
  auto& scene=std::get<wz3::SceneFrame>(frame.commands[0].draw);auto mesh=std::make_shared<wz3::Mesh>(*scene.instances[0].mesh);
  for(auto& face:mesh->faces)face.material=0;scene.instances[0].mesh=mesh;wz3::MaterialPass pass;pass.shader.words[0]=0x308;pass.shader.words[9]=0x10;pass.shader.words[28]=0xffffffff;scene.materials[0].passes={pass};
  try {renderer.render(frame);}catch(const std::runtime_error& e) {rejected=std::string_view(e.what()).find("depth bias")!=std::string_view::npos;}
  check(rejected,"scene geometry was silently substituted for saved material drawing");
  const auto pixels=renderer.render(frame,true);check(pixels[(16*32+16)*4]>0,"diagnostic scene geometry failed to render");
}
void particle_materials() {
  FrameFixture x;const auto material=x.material(0xffffffff,0x2200);x.p.operators[material].parameters[19]=0x40;x.p.operators[material].parameters[0]&=~2u;
  const auto effect=x.add(0x64,{0,16,0xff804020,0xff804020,0xff804020,f(1),f(1),f(1),f(.5f),0,0,0},{},{material});
  const auto emitter=x.add(0x63,{0,0,1,f(1),f(100),0,0,0,0,0,0,0,0,0,0,0,0});
  const auto scene=x.add(0xc1,{}, {effect,emitter}),viewport=x.viewport();x.g.nodes[viewport].inputs={scene};
  const auto root=x.add(0x0d,{}, {viewport});wz3::FramePlayer player(x.p,x.g);wz3::FrameRenderer renderer(x.p,x.g);
  auto frame=player.frame(root,0,64,64,10);const auto& sc=std::get<wz3::SceneFrame>(frame.commands[0].draw);
  check(sc.instances.empty() && sc.effects.size()==1 && sc.effects[0].vertices.size()==4,"GPU particle fixture must contain a real effect quad");
  const auto pixels=renderer.render(frame,true);pixel(pixels,64,32,32,{128,64,32});pixel(pixels,64,0,0,{0,0,0});
  // Particle materials must retain depth testing. Seed the same target's depth
  // with a frontmost opaque Layer2D, then clear only color before the scene.
  auto& rendered=std::get<wz3::SceneFrame>(frame.commands[0].draw);rendered.viewport_flags=1;
  wz3::FrameQuad front;front.shader.words[0]=0x20102;front.shader.words[9]=0x10;front.shader.words[28]=0xffffffff;front.depth=.01f;
  for(unsigned i=0;i<4;++i)front.shader.words[24+i]=f(1);front.shader.words[34]=front.shader.words[35]=front.shader.words[36]=front.shader.words[43]=front.shader.words[44]=f(1);
  frame.commands.insert(frame.commands.begin(),{root,frame.commands[0].target,front});
  const auto occluded=renderer.render(frame,true);pixel(occluded,64,32,32,{0,0,0});
}
void credits(const char* path,const char* font) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);const wz3::TextureOptions options{font};
  wz3::FramePlayer player(p,g,options);wz3::FrameRenderer renderer(p,g,options);
  for(int beat:{514,552,590}) {
    auto frame=player.frame(g.names.at("root"),beat*65536,320,240);const auto text=renderer.render(frame,true);bool found=false;
    for(auto& command:frame.commands)if(auto* scene=std::get_if<wz3::SceneFrame>(&command.draw))for(auto& effect:scene->effects)if(effect.operator_id==1774) {effect.vertices.clear();found=true;}
    check(found,"credits GPU test did not find Print");const auto background=renderer.render(frame,true);unsigned changed=0;
    for(std::size_t i=0;i<text.size();i+=4)changed+=text[i]!=background[i] || text[i+1]!=background[i+1] || text[i+2]!=background[i+2];
    // At beat 514 the entering text is almost entirely behind the bottom bar.
    if(beat!=514)check(changed>100,"credits text did not contribute visible pixels");std::cout<<"Theta credits at "<<beat<<": "<<changed<<" glyph pixels\n";
  }
}
void normalized_effect() {
  FrameFixture fixture;wz3::FrameRenderer renderer(fixture.p,fixture.g);wz3::DemoFrame frame;frame.width=frame.height=64;frame.surfaces[0]={64,64};
  wz3::SceneFrame scene;scene.viewport_flags=3;wz3::SceneEffectDraw text;text.normalized=true;text.shader.words[0]=0x20000;text.shader.words[9]=0x10;text.shader.words[28]=0xffff0000;
  for(unsigned i=0;i<4;++i)text.shader.words[24+i]=f(1);text.shader.words[34]=text.shader.words[35]=text.shader.words[36]=text.shader.words[43]=text.shader.words[44]=f(1);
  text.vertices={{{.25f,-.25f,0},{0,0},0xffffffff},{{.75f,-.25f,0},{1,0},0xffffffff},{{.75f,-.75f,0},{1,1},0xffffffff},{{.25f,-.75f,0},{0,1},0xffffffff}};
  scene.effects.push_back(text);frame.commands.push_back({0,{0,64,64},scene});const auto pixels=renderer.render(frame,true);pixel(pixels,64,16,16,{255,0,0});pixel(pixels,64,32,32,{0,0,0});
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::FramePlayer player(p,g);wz3::FrameRenderer renderer(p,g);
  for(int beat:{56,72,96,120,144,184,216,256,384,448,480,511}) {
    const auto frame=player.frame(g.names.at("root"),beat*65536,320,240);const auto pixels=renderer.render(frame,true);
    unsigned visible=0;for(std::size_t i=0;i<pixels.size();i+=4)visible+=pixels[i] || pixels[i+1] || pixels[i+2];
    check(visible>1000,"Theta composed frame contains too little visible geometry");
    pixel(pixels,320,160,0,{0,0,0});pixel(pixels,320,160,239,{0,0,0});
    std::cout<<"Theta beat "<<beat<<": "<<visible<<" visible pixels\n";
  }
}
}
int main(int argc,char** argv) {
  try {layers();released_layer_centers();released_layer_uv();targets();release_depth();blur_and_merge();crashzoom_and_merge();scene_opt_in();particle_materials();normalized_effect();if(argc>1)theta(argv[1]);if(argc>2)credits(argv[1],argv[2]);}
  catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
