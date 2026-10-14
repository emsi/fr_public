// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/frame.hpp"
#include "wz3/texture.hpp"
#include "wz3/playback_timing.hpp"
#include <charconv>
#include <chrono>
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
  if(argc<5) {std::cerr<<"Usage: "<<argv[0]<<" PROJECT.k|theta.wz3r ROOT_OR_ID BEATS OUTPUT [--diagnostic-geometry] [--preroll-ms STEP] [--font-file FONT.ttf]\n";return 2;}
  try {
    [[maybe_unused]] bool diagnostic=false;unsigned step=0;wz3::TextureOptions options;
    for(int i=5;i<argc;++i) {
      const std::string_view option=argv[i];
      if(option=="--diagnostic-geometry")diagnostic=true;
      else if(option=="--preroll-ms" && i+1<argc) {step=number<unsigned>(argv[++i]);if(!step || step>1000)throw std::runtime_error("preroll step must be 1..1000 ms");}
      else if(option=="--font-file" && i+1<argc)options.font_file=argv[++i];
      else throw std::runtime_error("unknown frame option");
    }
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);
    const auto found=graph.names.find(argv[2]);const auto root=found==graph.names.end() ? number<std::size_t>(argv[2]) : found->second;
    const auto time=number<double>(argv[3])*65536;
    if(!std::isfinite(time) || time<-2147483648.0 || time>2147483647.0)throw std::runtime_error("beat outside 16.16 range");
    wz3::FramePlayer player(project,graph,options);
#if WZ3_GPU_FRAME
    wz3::FrameRenderer renderer(project,graph,options);
#endif
    const wz3::PlaybackTiming timing(project,44100);
    const auto end_sample=timing.samples_at_beat(std::int32_t(time));
    const auto end=timing.milliseconds(end_sample);
    std::size_t preroll_frames=0;const auto started=std::chrono::steady_clock::now();
    if(step) {
      if(std::uint64_t(end)/step>100000)throw std::runtime_error("preroll frame limit");
      for(std::uint64_t ms=0;ms*44100/1000<end_sample;ms+=step) {
        const auto sample=ms*44100/1000;const auto beat=timing.beat(sample);
        try {
          const auto previous=player.frame(root,beat,1024,768,timing.milliseconds(sample));
#if WZ3_GPU_FRAME
          renderer.draw(previous,diagnostic);
#endif
        }catch(const std::exception& e){throw std::runtime_error("preroll at "+std::to_string(ms)+" ms (beat "+std::to_string(double(beat)/65536)+"): "+e.what());}
        ++preroll_frames;
        if(preroll_frames==1 || ms/10000!=(ms-step)/10000)std::cerr<<"Preroll "<<ms<<"/"<<end<<" ms, "<<preroll_frames<<" frames\n";
      }
    }
    const auto frame=player.frame(root,std::int32_t(time),1024,768,end);
#if WZ3_GPU_FRAME
    const auto pixels=renderer.render(frame,diagnostic);
    wz3::write_ppm(argv[4],frame.width,frame.height,pixels);
    if(diagnostic)std::cout<<"Diagnostic scene geometry; saved particle, Print, IPP and Layer2D materials. Scene lighting/shadows are not rendered.\n";
#else
    std::ofstream out(argv[4]);wz3::write_frame(out,frame);
#endif
    if(step)std::cout<<preroll_frames+1<<" consecutive frames including final sample in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" seconds\n";
    std::cout<<frame.commands.size()<<" draw commands, "<<frame.visited_operators<<" frame visits, "<<frame.cache_hits
      <<" cache hits, "<<frame.screen_fallbacks<<" pool fallbacks\n";
  }catch(const std::exception& error) {std::cerr<<"frame: "<<error.what()<<'\n';return 1;}
}
