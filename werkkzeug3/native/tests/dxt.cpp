// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "dxt.hpp"
#include <algorithm>
#include <cfenv>
#include <cstring>
#include <iostream>
namespace original {
using sInt=int;using sU32=std::uint32_t;using sU16=std::uint16_t;using sU8=std::uint8_t;
constexpr int sTF_DXT1=8,sTF_DXT5=9,sTF_A8=2,sTF_I8=11,sTF_A8I8=12,sTF_Q8W8V8U8=5,sTIF_RENDERTARGET=1;
struct D3DLOCKED_RECT {int Pitch;void* pBits;};
struct IDirect3DTexture9 {
  unsigned width,height,format;std::vector<wz3::CompressedTextureLevel> levels;
  int LockRect(int level,D3DLOCKED_RECT* rect,int,int) {
    const auto w=std::max(1u,width>>level),h=std::max(1u,height>>level),stride=((w+3)/4)*(format==8 ? 8 : 16);
    levels.push_back({w,h,std::vector<std::uint8_t>(stride*((h+3)/4))});
    rect->Pitch=stride;rect->pBits=levels.back().blocks.data();return 0;
  }
  void UnlockRect(int){}
};
struct sHardTex {unsigned Format,Flags,XSize,YSize,MipLevels;IDirect3DTexture9* Tex;};
struct sSystem_ {std::array<sHardTex,1> Textures;void UpdateTexture(sInt,sU16*,sInt);};
int sGetPower2(unsigned value){return std::countr_zero(value);}
void sCopyMem4(void* d,const void* s,std::size_t n){std::memcpy(d,s,n*4);}
void sCompressDXTBlock(sU8* d,const sU32* s,bool alpha,int quality) {wz3::legacy::dxt_block(std::span<const sU32,16>(s,16),{d,alpha ? 16u : 8u},alpha,quality);}
#define sLINK_RYGDXT 1
#define sINTRO 1
#define sVERIFY(x) check(x,"invalid reference upload")
#define FAILED(x) ((x)!=0)
#include "dxt_upload_oracle.inc"
#undef FAILED
#undef sVERIFY
#undef sINTRO
#undef sLINK_RYGDXT
std::vector<wz3::CompressedTextureLevel> upload(const wz3::Texture& t) {
  IDirect3DTexture9 texture{t.width,t.height,t.format,{}};sSystem_ system{{sHardTex{t.format,0,t.width,t.height,t.mip_count,&texture}}};
  std::vector<sU16> input;for(auto p:t.pixels){std::swap(p[0],p[2]);input.insert(input.end(),p.begin(),p.end());}
  system.UpdateTexture(0,input.data(),t.mip_threshold);return texture.levels;
}
}
namespace {
void compare(const wz3::Texture& t) {
  const auto expected=original::upload(t),actual=wz3::compressed_texture_levels(t);
  check(expected.size()==actual.size(),"DXT mip count");
  for(std::size_t i=0;i<actual.size();++i)check(actual[i].width==expected[i].width && actual[i].height==expected[i].height && actual[i].blocks==expected[i].blocks,"DXT packing/mips differs from archived UpdateTexture");
}
void cases() {
  unsigned count=0;std::uint32_t random=223;
  for(auto [width,height]:{std::pair{1u,1u},{2u,2u},{4u,4u},{8u,8u},{16u,4u},{4u,16u},{32u,16u}})
    for(unsigned format:{8u,9u})for(unsigned quality:{0u,1u,2u})for(unsigned threshold:{0u,1u,4u,16u,17u,31u,32u})for(unsigned mips:{0u,1u,3u}) {
      wz3::Texture t;t.width=width;t.height=height;t.format=format;t.mip_count=mips;t.mip_threshold=(quality<<6)|threshold;t.pixels.resize(width*height);
      for(auto& p:t.pixels)for(auto& c:p){random=random*1664525+1013904223;c=random>>16;}
      compare(t);++count;
    }
  // Canonical constant-red endpoints and block sizes, including transparent
  // alpha in BC3. Color compression cannot silently fall back to RGBA pixels.
  wz3::Texture solid;solid.width=solid.height=4;solid.format=8;solid.mip_count=1;solid.pixels.assign(16,{32767,0,0,0});
  const auto red=wz3::compressed_texture_levels(solid)[0].blocks;
  check(red==std::vector<std::uint8_t>({0,248,0,248,170,170,170,170}),"constant red DXT1 block");
  solid.format=9;const auto alpha=wz3::compressed_texture_levels(solid)[0].blocks;
  check(alpha.size()==16 && alpha[0]==0 && alpha[1]==0 && std::equal(red.begin(),red.end(),alpha.begin()+8),"constant transparent-red DXT5 block");
  const auto saved=std::fegetround();std::fesetround(FE_DOWNWARD);const auto rounded=wz3::compressed_texture_levels(solid)[0].blocks;
  check(std::fegetround()==FE_DOWNWARD,"DXT changed caller rounding");std::fesetround(saved);check(rounded==alpha,"DXT depends on caller rounding");
  std::cout<<count<<" DXT1/DXT5 uploads match archived block packing, mip generation, wrapping and quality selection\n";
}
void debris(const char* path,const char* font) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);unsigned count=0;wz3::TextureOptions options{font};
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable) {
    const auto& op=p.operators[id];
    if(op.command==0x25 && (op.parameters[0]==8 || op.parameters[0]==9)){const auto texture=wz3::evaluate_texture(p,g,id,options);compare(*texture);++count;}
  }
  std::cout<<count<<" real Debris compressed textures and their mip chains match archived upload\n";
}
}
int main(int argc,char** argv){try{cases();if(argc==3)debris(argv[1],argv[2]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
