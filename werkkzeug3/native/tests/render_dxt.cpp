// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <iostream>
namespace {
std::array<int,4> decode(const wz3::CompressedTextureLevel& level,unsigned x,unsigned y,unsigned format) {
  const auto offset=((y/4)*((level.width+3)/4)+x/4)*(format==8 ? 8 : 16);
  const auto* block=level.blocks.data()+offset;const auto* color=block+(format==9 ? 8 : 0);
  const auto word=[](const std::uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
  const auto expand=[](unsigned c){return std::array<int,4>{int(((c>>11)&31)*255/31),int(((c>>5)&63)*255/63),int((c&31)*255/31),255};};
  std::array<std::array<int,4>,4> palette;const auto a=word(color),b=word(color+2);palette[0]=expand(a);palette[1]=expand(b);
  for(unsigned c=0;c<3;++c) {
    palette[2][c]=a>b || format==9 ? (2*palette[0][c]+palette[1][c])/3 : (palette[0][c]+palette[1][c])/2;
    palette[3][c]=a>b || format==9 ? (palette[0][c]+2*palette[1][c])/3 : 0;
  }
  palette[2][3]=palette[3][3]=255;
  const unsigned index=(y%4)*4+x%4;
  const std::uint32_t selectors=color[4]|(std::uint32_t(color[5])<<8)|(std::uint32_t(color[6])<<16)|(std::uint32_t(color[7])<<24);
  auto result=palette[(selectors>>(index*2))&3];
  if(format==9) {
    std::array<int,8> alpha{block[0],block[1]};
    if(alpha[0]>alpha[1])for(int i=2;i<8;++i)alpha[i]=((8-i)*alpha[0]+(i-1)*alpha[1])/7;
    else {for(int i=2;i<6;++i)alpha[i]=((6-i)*alpha[0]+(i-1)*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
    std::uint64_t bits=0;for(unsigned i=0;i<6;++i)bits|=std::uint64_t(block[2+i])<<(i*8);
    result[3]=alpha[(bits>>(index*3))&7];
  }
  return result;
}
void test(unsigned format,unsigned threshold) {
  FrameFixture fixture;const auto gradient=fixture.add(0x34,{4,4,0x031beae3,0xfaf53e12,f(.18f),f(.46f),f(1.4f),0});
  const auto bitmap=fixture.add(0x25,{format,0,threshold},{gradient}),mat=fixture.material(0xffffffff);
  auto& p=fixture.p.operators[mat].parameters;p[9]=0;p[11]=0x10;p[23]=4;p[4]=0x104;
  fixture.g.nodes[mat].links={bitmap};const auto root=fixture.add(0x0d,{}, {fixture.layer(mat,4)});
  const auto texture=wz3::evaluate_texture(fixture.p,fixture.g,bitmap);const auto levels=wz3::compressed_texture_levels(*texture);
  wz3::FramePlayer player(fixture.p,fixture.g);wz3::FrameRenderer renderer(fixture.p,fixture.g);
  for(unsigned lod=0;lod<levels.size();++lod) {
    const auto size=levels[lod].width;const auto pixels=renderer.render(player.frame(root,0,size,size));
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
      const auto expected=decode(levels[lod],x,y,format);
      for(unsigned c=0;c<4;++c)if(std::abs(int(pixels[(y*size+x)*4+c])-expected[c])>2)
        throw std::runtime_error("GPU DXT "+std::to_string(format)+" mip "+std::to_string(lod)+" pixel "+std::to_string(x)+","+std::to_string(y)+" channel "+std::to_string(c)+": expected "+std::to_string(expected[c])+", got "+std::to_string(pixels[(y*size+x)*4+c]));
    }
  }
  check(renderer.stats().bitmap_uploads==1,"DXT was reuploaded across frames");
}
}
int main(){try{for(unsigned format:{8u,9u})for(unsigned threshold:{0u,64u,81u})test(format,threshold);std::cout<<"GPU DXT1/DXT5 sampling including alpha and tiny mip levels matches independent block decoder\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
