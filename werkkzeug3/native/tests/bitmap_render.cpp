// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <iostream>
namespace source {
using sInt=int;using sU32=std::uint32_t;using sU16=std::uint16_t;
#include "bitmap_render_oracle.inc"
}
namespace {
template<class F> void rejects(F action) {
  bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}
  check(caught,"invalid bitmap Render accepted");
}
void readback() {
  constexpr unsigned w=16,h=16,pitch=19;
  std::vector<std::uint32_t> bgra(pitch*h,0xdeadbeef);
  std::vector<std::uint8_t> rgba(w*h*4);
  for(unsigned i=0;i<w*h;++i) {
    std::uint32_t color=0;
    constexpr unsigned shifts[]={16,8,0,24};
    for(unsigned c=0;c<4;++c){const auto byte=(i*(2*c+1)+c*37)&255;rgba[i*4+c]=byte;color|=byte<<shifts[c];}
    bgra[(i/w)*pitch+i%w]=color;
  }
  std::vector<std::uint16_t> packed(w*h*4);source::readback(bgra.data(),packed.data(),w,h,pitch*4);
  std::vector<wz3::Pixel> expected(w*h);
  for(unsigned i=0;i<w*h;++i)expected[i]={packed[4*i+2],packed[4*i+1],packed[4*i],packed[4*i+3]};
  const auto actual=wz3::bitmap_readback(w,h,rgba);
  check(actual.pixels==expected,"readback differs from original padded BGRA loop");
  check(actual.format==1 && actual.mip_count==0 && actual.mip_threshold==0,"readback texture defaults");
  check(wz3::rgba8(actual)==rgba,"readback byte round trip");
  rejects([&]{wz3::bitmap_readback(0,h,rgba);});rejects([&]{wz3::bitmap_readback(4097,h,rgba);});
  rgba.pop_back();rejects([&]{wz3::bitmap_readback(w,h,rgba);});
}
struct Provider final : wz3::RenderedBitmapProvider {
  unsigned calls{};bool malformed=false;
  std::shared_ptr<const wz3::Texture> get(std::size_t id,std::size_t input,unsigned w,unsigned h)override {
    check(id==1 && input==0 && w==4 && h==2,"bitmap backend request");++calls;
    auto image=std::make_shared<wz3::Texture>();image->width=w;image->height=h;
    image->pixels.assign(w*h,wz3::Pixel{128,257,386,32767});if(malformed)image->pixels.pop_back();return image;
  }
  Stats stats()const override{return {};}
};
void graph() {
  FrameFixture x;x.add(0xf0);const auto render=x.add(0x3f,{2,1},{0});
  const auto invert=x.add(0x23,{4,0},{render}),merge=x.add(0x24,{0},{render,invert,render});
  auto provider=std::make_shared<Provider>();wz3::TextureOptions options;options.rendered_bitmaps=provider;
  wz3::TextureEvaluationStats stats;auto texture=wz3::evaluate_texture(x.p,x.g,merge,options,&stats);
  const wz3::Pixel p{128,257,386,32767};const auto expected=wz3::merge_pixel(wz3::merge_pixel(p,wz3::color_pixel(p,{},4),0),p,0);
  check(stats.operators==3 && provider->calls==1,"Render traversed its IPP input as a bitmap or rebaked a shared node");
  check(std::all_of(texture->pixels.begin(),texture->pixels.end(),[&](auto v){return v==expected;}),"15-bit downstream bitmap filters");
  rejects([&]{wz3::evaluate_texture(x.p,x.g,render);});
  provider->malformed=true;rejects([&]{wz3::evaluate_texture(x.p,x.g,render,options);});provider->malformed=false;
  x.g.nodes[render].links={0};rejects([&]{wz3::evaluate_texture(x.p,x.g,render,options);});x.g.nodes[render].links.clear();
  x.g.nodes[render].inputs.clear();rejects([&]{wz3::evaluate_texture(x.p,x.g,render,options);});
}
void environment() {
  FrameFixture x;const auto view=x.viewport(4);x.animate(view,0,6);
  wz3::FramePlayer player(x.p,x.g),baker(x.p,x.g);
  const auto playback=player.frame(view,2*65536,64,32,1000),bake=baker.bitmap_frame(view,64,32);
  const auto& a=std::get<wz3::SceneFrame>(playback.commands.at(0).draw);const auto& b=std::get<wz3::SceneFrame>(bake.commands.at(0).draw);
  check(a.camera.zoom_y==2 && b.camera.zoom_y==1,"bake inherited display aspect");
  check(a.camera.world[12]==2 && b.camera.world[12]==0 && bake.beat==0 && bake.milliseconds==0,"bake inherited playback clock");
  const auto event=x.event(view,"scheduled",0,65536);
  wz3::FramePlayer normal(x.p,x.g),fresh(x.p,x.g);
  check(normal.frame(event,0,32,32,0).commands.size()==1,"event fixture did not execute");
  check(fresh.bitmap_frame(event,32,32).commands.empty(),"bake activated project events");
  const auto demo=x.add(0x0d,{}, {view});wz3::FramePlayer invalid(x.p,x.g);
  rejects([&]{invalid.bitmap_frame(demo,32,32);});
}
}
int main(){try{readback();graph();environment();std::cout<<"Bitmap Render matches original readback for every byte value; graph boundary, filters, fresh clock/aspect/events and invalid inputs pass\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
