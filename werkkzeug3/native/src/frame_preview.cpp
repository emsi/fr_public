// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/frame.hpp"
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
  if(argc<5 || argc>6) {std::cerr<<"Usage: "<<argv[0]<<" PROJECT.k ROOT_OR_ID BEATS OUTPUT [--diagnostic-geometry]\n";return 2;}
  try {
    const bool diagnostic=argc==6 && std::string_view(argv[5])=="--diagnostic-geometry";
    if(argc==6 && !diagnostic)throw std::runtime_error("unknown frame option");
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);
    const auto found=graph.names.find(argv[2]);const auto root=found==graph.names.end() ? number<std::size_t>(argv[2]) : found->second;
    const auto time=number<double>(argv[3])*65536;
    if(!std::isfinite(time) || time<-2147483648.0 || time>2147483647.0)throw std::runtime_error("beat outside 16.16 range");
    wz3::FramePlayer player(project,graph);const auto frame=player.frame(root,std::int32_t(time),1024,768);
#if WZ3_GPU_FRAME
    wz3::FrameRenderer renderer(project,graph);const auto pixels=renderer.render(frame,diagnostic);
    wz3::write_ppm(argv[4],frame.width,frame.height,pixels);
    if(diagnostic)std::cout<<"Diagnostic scene geometry; saved IPP and Layer2D materials. Scene lighting/shadows are not rendered.\n";
#else
    std::ofstream out(argv[4]);wz3::write_frame(out,frame);
#endif
    std::cout<<frame.commands.size()<<" draw commands, "<<frame.visited_operators<<" frame visits, "<<frame.cache_hits
      <<" cache hits, "<<frame.screen_fallbacks<<" pool fallbacks\n";
  }catch(const std::exception& error) {std::cerr<<"frame: "<<error.what()<<'\n';return 1;}
}
