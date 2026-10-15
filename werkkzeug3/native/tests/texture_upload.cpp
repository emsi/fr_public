// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/lighting.hpp"
#include "wz3/playback_timing.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>

namespace {
void word(std::ostream& out,std::uint32_t value,unsigned bytes=4) {
  for(unsigned i=0;i<bytes;++i)out.put(char(value>>(8*i)));
}
std::uint32_t word(std::istream& in,unsigned bytes=4) {
  std::uint32_t value=0;for(unsigned i=0;i<bytes;++i){const auto c=in.get();check(c!=EOF,"truncated upload fixture");value|=std::uint32_t(c)<<(8*i);}return value;
}
std::vector<wz3::ColorTextureLevel> uploaded(const wz3::Texture& t) {
  if(t.format==1)return wz3::color_texture_levels(t);
  std::vector<wz3::ColorTextureLevel> result;
  for(const auto& level:wz3::signed_normal_levels(t,true)) {
    wz3::ColorTextureLevel out{level.width,level.height,{}};
    for(auto c:level.pixels)out.pixels.push_back(std::uint8_t(c));result.push_back(std::move(out));
  }
  return result;
}
void golden() {
  wz3::Texture t;t.width=t.height=4;t.pixels.resize(16);
  for(unsigned i=0;i<16;++i)for(unsigned c=0;c<4;++c)t.pixels[i][c]=(i*2517+c*5057)&32767;
  const auto pixels=t.pixels;auto levels=uploaded(t);
  check(levels.size()==3 && levels[1].pixels==std::vector<std::uint8_t>{49,88,128,167,88,128,167,143,206,117,157,69,117,157,68,108},"original 15-bit color mip reduction");
  check(levels[2].pixels==std::vector<std::uint8_t>{115,122,130,122},"original final color mip");
  check(t.pixels==pixels,"upload changed procedural source pixels");
  t.mip_threshold=4;check(uploaded(t)[2].pixels==std::vector<std::uint8_t>{0,39,79,118},"point mip threshold ignored");
  t.mip_threshold=16;check(uploaded(t)[2].pixels==std::vector<std::uint8_t>{0,39,79,118},"reversed mip threshold ignored");
  t.mip_count=1;check(uploaded(t).size()==1,"explicit mip limit ignored");
  t.mip_count=t.mip_threshold=0;t.format=5;levels=uploaded(t);
  check(levels[1].pixels==std::vector<std::uint8_t>{142,199,0,39,166,0,90,15,118,241,44,197,237,56,144,236},"original signed normal conversion");
  check(levels[2].pixels==std::vector<std::uint8_t>{140,210,23,250},"original final normal mip");
  t.format=1;t.width=1;t.height=2;t.pixels={{32768,65535,127,128},{0,0,0,0}};
  levels=uploaded(t);check(levels.size()==1 && levels[0].pixels[0]==0 && levels[0].pixels[1]==255 && levels[0].pixels[2]==0 && levels[0].pixels[3]==1,"byte packing and one-dimensional stop");
  t.pixels.pop_back();bool rejected=false;try{uploaded(t);}catch(const std::runtime_error&){rejected=true;}check(rejected,"invalid texture size accepted");
}
void export_inputs(const char* project_path,const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);std::ofstream manifest(directory/"inputs.txt");
  check(bool(manifest),"cannot write upload fixture manifest");std::size_t fixtures=0;
  const auto save=[&](const std::string& name,const wz3::Texture& t) {
    manifest<<name<<' '<<t.width<<' '<<t.height<<' '<<t.format<<' '<<t.mip_count<<' '<<t.mip_threshold<<'\n';
    std::ofstream out(directory/(name+".rgba16"),std::ios::binary);for(const auto& p:t.pixels)for(auto c:p)word(out,c,2);
    check(bool(out),"cannot write upload fixture pixels");++fixtures;
  };
  unsigned serial=0;
  for(const auto size:{std::pair{1u,1u},std::pair{8u,8u},std::pair{16u,4u},std::pair{4u,16u},std::pair{32u,16u}})
    for(unsigned fmt:{1u,5u})for(unsigned threshold:{0u,1u,4u,16u,17u,31u})for(unsigned count:std::set<unsigned>{0,1,std::min<unsigned>(3u,std::bit_width(std::max(size.first,size.second)))}) {
      wz3::Texture t;t.width=size.first;t.height=size.second;t.format=fmt;t.mip_count=count;t.mip_threshold=threshold;t.pixels.resize(t.width*t.height);
      std::uint32_t state=1;for(auto& p:t.pixels)for(auto& c:p){state=state*1664525+1013904223;c=state>>16;}
      t.pixels[0]={16384,16384,16384,65535};save("fixture-"+std::to_string(serial++),t);
    }
  const auto project=wz3::read_project(project_path);check(project.runtime_graph,"upload audit needs released Theta");const auto graph=wz3::connect(project);
  wz3::FramePlayer player(project,graph);wz3::PlaybackTiming timing(project,44100);std::set<std::size_t> seen;
  const auto remember=[&](std::size_t id,std::shared_ptr<const wz3::Texture> supplied={}) {
    for(unsigned depth=0;depth<512;++depth) {
      const auto cmd=project.operators.at(id).command;
      if(cmd==1)id=*graph.nodes.at(id).links.at(0);
      else if(cmd==2 || cmd==3)id=graph.nodes.at(id).inputs.at(0);
      else break;
    }
    if(project.operators.at(id).command==0x26 || !seen.insert(id).second)return;
    const auto t=supplied ? supplied : wz3::evaluate_texture(project,graph,id);
    save("texture-"+std::to_string(id),*t);
  };
  std::size_t frames=0;const auto end=timing.end_frames();
  for(std::uint64_t ms=0;;ms+=16) {
    const auto sample=std::min(ms*44100/1000,end);const auto frame=player.frame(graph.names.at("root"),timing.beat(sample),1024,768,timing.milliseconds(sample));++frames;
    for(const auto& c:frame.commands) {
      if(const auto* q=std::get_if<wz3::FrameQuad>(&c.draw)){for(const auto& t:q->textures)if(t && !t->surface)remember(t->id);}
      else if(const auto* scene=std::get_if<wz3::SceneFrame>(&c.draw)) {
        for(const auto& [id,m]:scene->materials){static_cast<void>(id);for(const auto& pass:m.passes)for(auto t:pass.shader.textures)if(t)remember(*t);}
        for(const auto& effect:scene->effects)for(auto t:effect.shader.textures)if(t)remember(*t,effect.textures.contains(*t) ? effect.textures.at(*t) : nullptr);
      }
    }
    if(sample==end)break;
  }
  check(bool(manifest),"cannot finish upload fixture manifest");
  std::cout<<"Exported "<<fixtures<<" upload fixtures, including "<<seen.size()<<" textures referenced over "<<frames<<" frames\n";
}
void verify(const std::filesystem::path& directory,const std::filesystem::path& original) {
  std::ifstream manifest(directory/"inputs.txt");check(bool(manifest),"missing upload fixture manifest");std::string name;std::size_t cases=0,levels=0,bytes=0;
  while(manifest>>name) {
    wz3::Texture t;manifest>>t.width>>t.height>>t.format>>t.mip_count>>t.mip_threshold;
    check(bool(manifest) && t.width<=1024 && t.height<=1024,"invalid fixture dimensions");
    std::ifstream input(directory/(name+".rgba16"),std::ios::binary);t.pixels.resize(std::size_t(t.width)*t.height);for(auto& p:t.pixels)for(auto& c:p)c=word(input,2);
    std::ifstream reference(original/(name+".upload"),std::ios::binary);char magic[8]{};reference.read(magic,8);check(std::string_view(magic,8)==std::string_view("THUPLD1\0",8),"invalid original upload trace");
    check(word(reference)==t.width && word(reference)==t.height && word(reference)==t.format && word(reference)==t.mip_count && word(reference)==t.mip_threshold,"original fixture metadata mismatch");
    const auto actual=uploaded(t);check(word(reference)==actual.size(),"original mip count mismatch");
    for(const auto& level:actual) {
      check(word(reference)==level.width && word(reference)==level.height && word(reference)==level.pixels.size(),"original mip dimensions mismatch");
      std::vector<std::uint8_t> pixels(level.pixels.size());reference.read(reinterpret_cast<char*>(pixels.data()),pixels.size());check(bool(reference),"truncated mip pixels");
      if(pixels!=level.pixels)throw std::runtime_error("original upload differs: "+name+" "+std::to_string(level.width)+"x"+std::to_string(level.height));
      bytes+=pixels.size();++levels;
    }
    check(reference.peek()==EOF,"trailing original mip data");++cases;
  }
  check(manifest.eof() && cases,"empty or invalid upload manifest");std::cout<<cases<<" original texture uploads, "<<levels<<" mip levels and "<<bytes<<" bytes match exactly\n";
}
}
int main(int argc,char** argv) {
  try {
    if(argc==1)golden();
    else if(argc==4 && std::string_view(argv[1])=="--export")export_inputs(argv[2],argv[3]);
    else if(argc==4 && std::string_view(argv[1])=="--verify")verify(argv[2],argv[3]);
    else throw std::runtime_error("usage: wz3-texture-upload-tests [--export PROJECT DIRECTORY | --verify FIXTURES ORIGINAL]");
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
