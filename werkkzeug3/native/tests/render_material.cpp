// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/material.hpp"
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::uint32_t f(float value) {return std::bit_cast<std::uint32_t>(value);}
wz3::Material base(std::uint32_t color=0xff204080) {
  wz3::Material m;m.passes.resize(1);auto& w=m.passes[0].shader.words;w[9]=0x10;w[28]=color;return m;
}
std::array<float,4> color(std::uint32_t value) {return {float((value>>16)&255)/255,float((value>>8)&255)/255,float(value&255)/255,float(value>>24)/255};}
void pixel(const std::vector<std::uint8_t>& image,unsigned x,unsigned y,std::array<float,4> expected) {
  for(unsigned c=0;c<4;++c)if(std::abs(int(image[(y*16+x)*4+c])-int(std::lround(std::clamp(expected[c],0.0f,1.0f)*255)))>1)
    throw std::runtime_error("material pixel mismatch at "+std::to_string(x)+","+std::to_string(y)+" channel "+std::to_string(c));
}
void combiners() {
  auto material=base();auto& w=material.passes[0].shader.words;w[29]=0x40402010;
  const auto a=color(w[28]),b=color(w[29]);
  for(unsigned op=1;op<=12;++op) {
    w[10]=op<<4;std::array<float,4> result{};result[3]=1;
    for(unsigned c=0;c<3;++c)switch(op) {
    case 1:result[c]=b[c];break;case 2:result[c]=a[c]+b[c];break;case 3:result[c]=a[c]-b[c];break;
    case 4:case 5:case 6:case 7:result[c]=a[c]*b[c]*(1u<<(op-4));break;
    case 8:result[c]=a[c]*(1-b[c])+b[c];break;case 9:result[c]=a[c];break;
    case 10:result[c]=b[c]-a[c];break;case 11:result[c]=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];break;
    case 12:result[c]=std::clamp(a[c]*b[c],0.0f,1.0f);break;
    }
    const auto image=wz3::render_material_quad(material,{},16,16);pixel(image,8,8,result);
  }
  w[10]=0x99;auto image=wz3::render_material_quad(material,{},16,16);std::array<float,4> lerp{};lerp[3]=1;
  for(unsigned c=0;c<3;++c)lerp[c]=b[3]*a[c]+(1-b[3])*b[c];pixel(image,8,8,lerp);
  w[10]=0x29;image=wz3::render_material_quad(material,{},16,16);std::array<float,4> fade{};fade[3]=1;
  for(unsigned c=0;c<3;++c)fade[c]=a[c]+b[c]*b[3];pixel(image,8,8,fade);
  w[10]=0;w[23]=0x199;image=wz3::render_material_quad(material,{},16,16);auto alpha=a;alpha[3]=(1-b[3])*b[3];pixel(image,8,8,alpha);
  w[0]=1;w[23]=8;w[28]=0x80204080;image=wz3::render_material_quad(material,{},16,16,0xff102030);pixel(image,8,8,color(0xff102030));
  w[28]=0x81204080;image=wz3::render_material_quad(material,{},16,16);pixel(image,8,8,color(w[28]));
  w[0]=w[23]=0;w[28]=w[29]=w[30]=w[31]=0xffcccccc;w[10]=0x20;w[15]=0xc0;w[16]=0x30;
  image=wz3::render_material_quad(material,{},16,16);pixel(image,8,8,{.2f,.2f,.2f,1});
}
void blends() {
  constexpr auto background=0x60806040u;auto material=base(0x80402010);auto& w=material.passes[0].shader.words;w[23]=8;
  const auto s=color(w[28]),d=color(background);
  for(unsigned mode=0;mode<=9;++mode) {
    w[0]=mode<<12;std::array<float,4> expected{};
    for(unsigned c=0;c<4;++c)switch(mode) {
    case 0:expected[c]=s[c];break;case 1:expected[c]=s[c]*s[3]+d[c]*(1-s[3]);break;
    case 2:expected[c]=d[c]+s[c];break;case 3:expected[c]=d[c]*s[c];break;case 4:expected[c]=2*d[c]*s[c];break;
    case 5:expected[c]=s[c]+d[c]*(1-s[c]);break;case 6:expected[c]=d[c]-s[c];break;
    case 7:expected[c]=s[c]*(1-d[c])+d[c]*(1-s[c]);break;case 8:expected[c]=d[c]+s[c]*d[3];break;
    case 9:expected[c]=d[c]+s[c]*s[3];break;
    }
    pixel(wz3::render_material_quad(material,{},16,16,background),8,8,expected);
  }
}
void textures() {
  auto texture=std::make_shared<wz3::Texture>();texture->width=texture->height=2;
  texture->pixels={wz3::color64(0xffff0000),wz3::color64(0xff00ff00),wz3::color64(0xff0000ff),wz3::color64(0xffffffff)};
  std::map<std::size_t,std::shared_ptr<const wz3::Texture>> textures={{12,texture}};auto material=base();auto& s=material.passes[0].shader;s.textures[0]=12;s.words[9]=0;s.words[11]=16;
  auto image=wz3::render_material_quad(material,textures,16,16);pixel(image,2,2,{1,0,0,1});pixel(image,12,2,{0,1,0,1});pixel(image,2,12,{0,0,1,1});pixel(image,12,12,{1,1,1,1});
  s.words[4]=0x1000;s.words[24]=f(2);image=wz3::render_material_quad(material,textures,16,16);pixel(image,2,2,{1,0,0,1});pixel(image,10,2,{1,0,0,1});
  s.words[4]|=0x100;image=wz3::render_material_quad(material,textures,16,16);pixel(image,10,2,{0,1,0,1});
  s.words[4]=0x2000;s.words[34]=s.words[35]=s.words[36]=f(1);s.words[40]=f(.5f);image=wz3::render_material_quad(material,textures,16,16);pixel(image,2,2,{0,1,0,1});
  s.words[4]=0x3000;s.words[43]=s.words[44]=f(1);s.words[45]=f(3.14159265358979323846f/2);s.words[47]=f(1);
  image=wz3::render_material_quad(material,textures,16,16);pixel(image,2,2,{0,0,1,1});pixel(image,12,2,{1,0,0,1});
  // Two slots share an image but retain independent sampler modes.
  s.words[4]=0x1000;s.words[24]=f(2);s.words[5]=0x1100;s.words[25]=f(2);s.textures[1]=12;s.words[12]=0x20;
  image=wz3::render_material_quad(material,textures,16,16);pixel(image,10,2,{1,1,0,1});
  s.words[23]=0xe0;s.words[28]=0x40ffffff;image=wz3::render_material_quad(material,textures,16,16);pixel(image,10,2,{1,1,0,128/255.0f});
}
}
int main() {
  try {combiners();blends();textures();std::cout<<"GLES material combiners, alpha, blend equations, UV transforms and sampler isolation passed\n";}
  catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
