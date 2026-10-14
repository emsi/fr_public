// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/material.hpp"
#include "wz3/texture.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
template<class T> T number(std::string_view text) {
  T value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if(error!=std::errc{} || end!=text.data()+text.size())throw std::runtime_error("invalid numeric argument");
  return value;
}
}
int main(int argc,char** argv) {
  if(argc!=3 && argc!=5 && argc!=6) {std::cerr<<"Usage: "<<argv[0]<<" PROJECT.k --audit\n       "<<argv[0]<<" PROJECT.k NAME_OR_ID BEATS OUTPUT [EVENT_INDEX]\n";return 2;}
  try {
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);wz3::MaterialRuntime materials(project,graph);
    if(argc==3 && std::string_view(argv[2])=="--audit") {
      std::size_t count=0,passes=0,updates=0;std::array<std::size_t,8> phases{};
      for(std::size_t i=0;i<project.operators.size();++i)if(graph.nodes[i].reachable && wz3::operator_info(project.operators[i].command).category=="MATERIAL") {
        try {
          const auto material=materials.snapshot(i);++count;passes+=material.passes.size();
          for(const auto& pass:material.passes)++phases.at(unsigned(pass.usage));
          for(auto beat:{0,16384,32768,49152,65536}) {auto env=wz3::frame_environment(beat);materials.execute(i,env);++updates;}
        }catch(const std::exception& error) {throw std::runtime_error("material "+std::to_string(i)+": "+error.what());}
      }
      std::cout<<"{\"materials\":"<<count<<",\"passes\":"<<passes<<",\"updates\":"<<updates<<",\"phases\":[";
      for(unsigned i=0;i<phases.size();++i)std::cout<<(i ? "," : "")<<phases[i];
      std::cout<<"]}\n";
    } else {
      if(argc<5)throw std::runtime_error("expected material selector, beat and output");
      const auto found=graph.names.find(argv[2]);const auto id=found==graph.names.end() ? number<std::size_t>(argv[2]) : found->second;
      const double time=number<double>(argv[3])*65536;
      if(!std::isfinite(time) || time<-2147483648.0 || time>2147483647.0)throw std::runtime_error("beat outside 16.16 range");
      const auto beat=std::int32_t(time);auto env=wz3::frame_environment(beat);const wz3::SplineLibrary splines(project);
      std::unique_ptr<wz3::EventAnimationScope> event;
      if(argc==6)event=std::make_unique<wz3::EventAnimationScope>(project.events.at(number<std::size_t>(argv[5])),beat,splines,env);
      materials.execute(id,env);const auto result=materials.snapshot(id);
#if WZ3_GPU_MATERIAL
      std::map<std::size_t,std::shared_ptr<const wz3::Texture>> textures;
      for(const auto& pass:result.passes) {
        wz3::material_fragment_shader(pass.shader);
        for(auto texture:pass.shader.textures)if(texture && !textures.contains(*texture))textures.emplace(*texture,wz3::evaluate_texture(project,graph,*texture));
      }
      const auto image=wz3::render_material_quad(result,textures,512,512);wz3::write_ppm(argv[4],512,512,image);
      std::cout<<"Material "<<id<<": "<<result.passes.size()<<" unlit passes on a flat quad; lighting and full-scene shading remain pending\n";
#else
      std::ofstream out(argv[4]);
      if(!out)throw std::runtime_error("cannot open material output");
      wz3::write_material(out,result);out<<'\n';
      std::cout<<"Material "<<id<<": "<<result.passes.size()<<" native render passes; state exported without GPU shading\n";
#endif
    }
  }catch(const std::exception& error) {std::cerr<<"material: "<<error.what()<<'\n';return 1;}
}
