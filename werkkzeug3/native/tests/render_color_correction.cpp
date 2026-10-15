// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <iostream>
namespace {
using namespace wz3;using Pixels=std::vector<std::uint8_t>;
std::array<std::uint32_t,11> parameters(){return {3,f(.1f),f(.8f),0x006080a0,0x00c08040,0xffd0a0f0,0xff100808,0,0x00182030,0x00302010,f(1.7f)};}
// Direct scalar interpretation of effect_colorcorrect.psh. Threshold selection
// weights c0 by the *high* gray fraction; alpha is untouched by every operation.
Pixels reference(Pixels pixels,const std::array<std::uint32_t,11>& p){
  const auto color=[](unsigned word,unsigned c){return ((word>>(c==0 ? 16 : c==1 ? 8 : 0))&255)/255.f;};
  for(unsigned i=0;i<pixels.size();i+=4){const float gray=(pixels[i]/255.f+pixels[i+1]/255.f+pixels[i+2]/255.f)/3;
    const float t=std::clamp((gray-as_float(p[1]))/(as_float(p[2])-as_float(p[1])),0.f,1.f);
    for(unsigned c=0;c<3;++c){float value=t;if(!(p[7]&1)){const auto original=pixels[i+c]/255.f;const auto dark=gray*color(p[8],c)+original*color(p[3],c),light=gray*color(p[9],c)+original*color(p[4],c);value=std::clamp(dark*t+light*(1-t)-color(p[6],c),0.f,1.f)*color(p[5],c)*as_float(p[10]);}pixels[i+c]=std::lround(std::clamp(value,0.f,1.f)*255);}}
  return pixels;
}
DemoFrame background(unsigned w,unsigned h){DemoFrame frame;frame.width=w;frame.height=h;frame.surfaces[0]={w,h};for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x){FrameQuad q;q.shader.words[0]=0x20002;q.shader.words[9]=0x10;q.shader.words[23]=8;q.shader.words[28]=(((x+y)*31%256)<<24)|((25+x*30)<<16)|((31+y*59)<<8)|(17+(x*83+y*11)%230);q.rectangle={x/8.f,y/4.f,(x+1)/8.f,(y+1)/4.f};frame.commands.push_back({0,{0,w,h},q});}return frame;}
SceneEffectDraw correction(std::array<std::uint32_t,11> p,unsigned pass=3){SceneEffectDraw draw;draw.sort_key=pass<<24;draw.color_correction=ColorCorrectionEffect{p};return draw;}
void check_pixels(const Pixels& a,const Pixels& b){check(a.size()==b.size(),"Color Correction image size");unsigned maximum=0;for(unsigned i=0;i<a.size();++i){maximum=std::max(maximum,unsigned(std::abs(int(a[i])-b[i])));if(i%4==3)check(a[i]==b[i],"Color Correction changed alpha");}if(maximum>2)throw std::runtime_error("Color Correction GPU/scalar mismatch: "+std::to_string(maximum));}
void cases(){FrameFixture fixture;FrameRenderer renderer(fixture.p,fixture.g);unsigned count=0;
  for(auto size:{std::array{128u,64u},std::array{64u,32u},std::array{256u,96u},std::array{32u,16u}})for(unsigned flag:{0u,1u})for(float amp:{0.f,.5f,1.f,2.75f})for(bool reverse:{false,true}){
    const auto [w,h]=size;auto frame=background(w,h);const auto original=renderer.render(frame);auto p=parameters();p[7]=flag;p[10]=f(amp);if(reverse)std::swap(p[1],p[2]);SceneFrame scene;scene.effects={correction(p)};frame.commands.push_back({0,{0,w,h},scene});const auto actual=renderer.render(frame);check_pixels(actual,reference(original,p));check(renderer.render(frame)==actual,"Color Correction changed on repeated frame");++count;
  }
  // A smaller viewport within a larger target updates only the captured area.
  auto frame=background(128,64);const auto original=renderer.render(frame);auto p=parameters();SceneFrame scene;scene.effects={correction(p)};frame.commands.push_back({0,{0,64,32},scene});const auto actual=renderer.render(frame);auto expected=original;
  for(unsigned y=0;y<32;++y){Pixels row(original.begin()+y*128*4,original.begin()+(y*128+64)*4);row=reference(row,p);std::copy(row.begin(),row.end(),expected.begin()+y*128*4);}check_pixels(actual,expected);
  const auto before=renderer.stats();renderer.reset_targets();check(renderer.render(frame)==actual,"Color Correction reset changed capture");check(renderer.stats().surface_allocations==before.surface_allocations && renderer.stats().program_compilations==before.program_compilations,"Color Correction did not reuse capture or shader");
  std::cout<<count<<" GPU Color Correction cases match scalar shader arithmetic; alpha, reversed thresholds, resized/cropped viewports and reuse pass\n";
}
SceneEffectDraw solid(std::uint32_t color,unsigned pass){SceneEffectDraw draw;draw.sort_key=pass<<24;draw.normalized=true;draw.shader.words[0]=0x20002;draw.shader.words[9]=0x10;draw.shader.words[23]=8;draw.shader.words[28]=color;draw.vertices={{{0,0,0},{0,0},0xffffffff},{{2,0,0},{1,0},0xffffffff},{{2,-2,0},{1,1},0xffffffff},{{0,-2,0},{0,1},0xffffffff}};return draw;}
void ordering(){FrameFixture fixture;FrameRenderer renderer(fixture.p,fixture.g);auto frame=background(64,32);const auto input=renderer.render(frame);auto p=parameters();SceneFrame scene;
  // Capture once at pass entry, before an earlier same-pass draw overwrites it.
  scene.effects={solid(0xffcc2200,3),correction(p,3)};frame.commands.push_back({0,{0,64,32},scene});check_pixels(renderer.render(frame),reference(input,p));
  auto q=p;q[10]=f(.5f);scene.effects={correction(p,3),correction(q,3)};std::get<SceneFrame>(frame.commands.back().draw)=scene;check_pixels(renderer.render(frame),reference(input,q));
  scene.effects[1].sort_key=4u<<24;std::get<SceneFrame>(frame.commands.back().draw)=scene;check_pixels(renderer.render(frame),reference(reference(input,p),q));
  // Glare overwrites shared target zero itself, even within one render pass.
  SceneEffectDraw glare;glare.sort_key=3u<<24;glare.glare=GlareEffect{{3,4,0,0,0,0,0,f(1),f(1),f(1),0}};
  scene.effects={solid(0xffcc2200,3),glare,correction(p,3)};std::get<SceneFrame>(frame.commands.back().draw)=scene;
  auto red=frame;auto& redscene=std::get<SceneFrame>(red.commands.back().draw);redscene.effects={solid(0xffcc2200,3)};const auto redpixels=renderer.render(red);
  check(redpixels!=input,"ordered fixture did not change image");check_pixels(renderer.render(frame),reference(redpixels,p));
  std::cout<<"Color Correction captures at pass boundaries, shares Glare capture and preserves same-pass effect ordering\n";
}
void debris(const char* path){const auto p=read_project(path);const auto g=connect(p);FrameRenderer renderer(p,g);unsigned count=0;for(std::size_t i=0;i<p.operators.size();++i)if(g.nodes[i].reachable && p.operators[i].command==0x75){auto frame=background(128,64);const auto input=renderer.render(frame);std::array<std::uint32_t,11> values;std::copy(p.operators[i].parameters.begin(),p.operators[i].parameters.end(),values.begin());SceneFrame scene;scene.effects={correction(values)};frame.commands.push_back({0,{0,128,64},scene});check_pixels(renderer.render(frame),reference(input,values));++count;}check(count==12,"missing Debris Color Correction GPU cases");std::cout<<count<<" saved Debris Color Correction effects match scalar reference\n";}
}
int main(int argc,char** argv){try{cases();ordering();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
