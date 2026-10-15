// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
namespace {
using Color=std::array<float,4>;using Pixels=std::vector<std::uint8_t>;
struct Image {
  unsigned width{},height{};Pixels pixels;
  Color sample(float u,float v) const {
    const float x=std::clamp(u*width-.5f,0.f,float(width-1)),y=std::clamp(v*height-.5f,0.f,float(height-1));
    const unsigned x0=unsigned(x),y0=unsigned(y),x1=std::min(x0+1,width-1),y1=std::min(y0+1,height-1);Color result;
    for(unsigned c=0;c<4;++c) {
      const auto get=[&](unsigned a,unsigned b){return pixels[(b*width+a)*4+c]/255.f;};
      const float a=get(x0,y0)+(get(x1,y0)-get(x0,y0))*(x-x0),b=get(x0,y1)+(get(x1,y1)-get(x0,y1))*(x-x0);
      result[c]=a+(b-a)*(y-y0);
    }
    return result;
  }
};
Image raster(unsigned w,unsigned h,const std::function<Color(float,float)>& shade) {
  Image result{w,h,Pixels(std::size_t(w)*h*4)};
  for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
    const auto value=shade((x+.5f)/w,(y+.5f)/h);
    for(unsigned c=0;c<4;++c)result.pixels[(y*w+x)*4+c]=std::lround(std::clamp(value[c],0.f,1.f)*255);
  }
  return result;
}
Color unpack(std::uint32_t c){return {((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f,(c>>24)/255.f};}
// Independent scalar evaluation of the three archived pixel shaders. Each
// returned Image quantizes to RGBA8 before the next stage samples it.
Image reference(const Image& input,std::span<const std::uint32_t> p,unsigned cw,unsigned ch) {
  Image captured{cw,ch,Pixels(std::size_t(cw)*ch*4)};
  for(unsigned y=0;y<ch;++y)for(unsigned x=0;x<cw;++x)captured.pixels[(y*cw+x)*4+3]=255;
  for(unsigned y=0;y<input.height;++y)std::copy_n(input.pixels.begin()+y*input.width*4,input.width*4,captured.pixels.begin()+y*cw*4);
  Image current=captured;float us=float(input.width)/cw,vs=float(input.height)/ch;
  const auto down=p[1]&3;const auto size=down==1 ? 256u : 512u;
  for(unsigned stage=0;stage<(down==2 ? 2u : 1u);++stage) {
    const unsigned w=stage ? 256 : size;
    auto result=raster(w,w/2,[&](float u,float v) {
      if(p[1]&4)return current.sample(u*us,v*vs);
      constexpr float weights[]={5.f/16,6.f/16,5.f/16},offsets[]={-1.2f,0,1.2f};Color sum{0,0,0,25.f/256};
      for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x) {
        const auto value=current.sample(u*us+offsets[x]/current.width,v*vs+offsets[y]/current.height);
        for(unsigned c=0;c<4;++c)sum[c]+=value[c]*(weights[x]*weights[y]);
      }
      return sum;
    });current=std::move(result);us=vs=1;
  }
  const auto threshold=unpack(p[2]);const float scale=1.f/((1-threshold[0])+(1-threshold[1])+(1-threshold[2])),mix=wz3::as_float(p[9]);
  current=raster(current.width,current.height,[&](float u,float v){auto value=current.sample(u,v);for(unsigned c=0;c<3;++c)value[c]-=threshold[c];const float gray=value[0]*scale+value[1]*scale+value[2]*scale;for(auto& c:value)c=c*mix+gray*(1-mix);return value;});
  constexpr float weights[]={1.f/25,2.f/25,3.f/25,4.f/25,5.f/25,4.f/25,3.f/25,2.f/25,1.f/25};
  for(unsigned stage=0;stage<2;++stage) {
    const float radius=wz3::as_float(p[5+stage]),amp=wz3::as_float(p[7+stage]);if(radius<=0)continue;
    for(unsigned vertical=0;vertical<2;++vertical)current=raster(current.width,current.height,[&](float u,float v) {
      const float gain=vertical ? amp : 1;Color sum{0,0,0,weights[0]*gain};
      for(unsigned tap=0;tap<9;++tap) {
        const float offset=(int(tap)-4)*radius;const auto value=current.sample(u+(vertical ? 0 : offset),v+(vertical ? offset*current.width/current.height : 0));
        for(unsigned c=0;c<4;++c)sum[c]+=value[c]*(weights[tap]*gain);
      }
      return sum;
    });
  }
  const auto cb=unpack(p[3]),co=unpack(p[4]);const float smooth=wz3::as_float(p[10]);
  return raster(input.width,input.height,[&](float u,float v){auto a=current.sample(u,v),b=captured.sample(u*input.width/cw,v*input.height/ch);for(unsigned c=0;c<4;++c){a[c]*=cb[c];b[c]*=co[c];a[c]=(a[c]-a[c]*b[c]*smooth)+b[c];}return a;});
}
wz3::DemoFrame background(unsigned w,unsigned h) {
  wz3::DemoFrame frame;frame.width=w;frame.height=h;frame.surfaces[0]={w,h};
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x) {
    wz3::FrameQuad q;q.shader.words[0]=0x20002;q.shader.words[9]=0x10;q.shader.words[23]=8;
    q.shader.words[28]=(((x+y)*31%256)<<24)|((25+x*30)<<16)|((31+y*59)<<8)|(17+(x*83+y*11)%230);
    q.rectangle={x/8.f,y/4.f,(x+1)/8.f,(y+1)/4.f};frame.commands.push_back({0,{0,w,h},q});
  }
  return frame;
}
void compare(wz3::FrameRenderer& renderer,unsigned w,unsigned h,unsigned cw,unsigned ch,const std::vector<std::uint32_t>& p,bool repeat=false) {
  auto frame=background(w,h);const auto original=renderer.render(frame);const auto expected=reference({w,h,original},p,cw,ch);
  wz3::SceneFrame scene;wz3::SceneEffectDraw effect;effect.glare.emplace();std::copy(p.begin(),p.end(),effect.glare->parameters.begin());scene.effects.push_back(effect);
  frame.commands.push_back({0,{0,w,h},scene});const auto actual=renderer.render(frame);unsigned maximum=0;std::uint64_t total=0;
  for(std::size_t i=0;i<actual.size();++i){const auto difference=unsigned(std::abs(int(actual[i])-expected.pixels[i]));maximum=std::max(maximum,difference);total+=difference;}
  if(maximum>3 || double(total)/actual.size()>.65)throw std::runtime_error("Glare GPU mismatch: max "+std::to_string(maximum)+", mean "+std::to_string(double(total)/actual.size()));
  if(repeat){const auto stats=renderer.stats();check(renderer.render(frame)==actual,"Glare changed across identical frames");renderer.reset_targets();check(renderer.render(frame)==actual,"Glare loading reset retained old pixels");check(renderer.stats()==stats,"Glare reallocated resources across frames/reset");}
  std::cout<<"Glare flags "<<p[1]<<" at "<<w<<'x'<<h<<": max error "<<maximum<<", mean "<<double(total)/actual.size()<<'\n';
}
void tests() {
  FrameFixture x;wz3::FrameRenderer renderer(x.p,x.g);std::vector<std::uint32_t> p={31,0,0x803f6b8d,0x83d6e8fa,0xa5a9b2c7,f(.0063f),f(.0021f),f(1.4f),f(.8f),f(.37f),f(.54f)};
  for(unsigned flags=0;flags<8;++flags){p[1]=flags;compare(renderer,512,256,512,256,p,true);}
  p[1]=2;compare(renderer,384,192,512,256,p);compare(renderer,768,384,768,384,p);compare(renderer,512,256,768,384,p,true);
  p[5]=p[6]=0;p[9]=f(1);p[10]=0;compare(renderer,512,256,768,384,p);
  p[5]=f(.003f);p[7]=f(.6f);p[9]=0;p[10]=f(1);compare(renderer,512,256,768,384,p);
  // Two effects must execute in their scene pass order, capture the preceding
  // result, and leave pixels outside a cropped viewport untouched.
  auto frame=background(512,256);const auto original=renderer.render(frame);Image crop{256,128,Pixels(256*128*4)};
  for(unsigned y=0;y<128;++y)std::copy_n(original.begin()+y*512*4,256*4,crop.pixels.begin()+y*256*4);
  auto second=p;second[1]=5;second[3]=0xff80f060;second[4]=0xffa06080;second[9]=f(.7f);
  const auto expected=reference(reference(crop,p,768,384),second,768,384);
  wz3::SceneFrame scene;
  for(const auto& [key,params]:{std::pair{8u,second},std::pair{4u,p}}){wz3::SceneEffectDraw effect;effect.sort_key=key<<24;effect.glare.emplace();std::copy(params.begin(),params.end(),effect.glare->parameters.begin());scene.effects.push_back(effect);}
  frame.commands.push_back({0,{0,256,128},scene});const auto actual=renderer.render(frame);
  for(unsigned y=0;y<256;++y)for(unsigned x=0;x<512;++x)for(unsigned c=0;c<4;++c) {
    const auto index=(y*512+x)*4+c;const int wanted=x<256 && y<128 ? expected.pixels[(y*256+x)*4+c] : original[index];
    check(std::abs(int(actual[index])-wanted)<=(x<256 && y<128 ? 3 : 0),"Glare cropped viewport or ordered recapture differs");
  }
}
}
int main(){try{tests();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
