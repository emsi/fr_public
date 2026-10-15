// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/v2m.hpp"
#include "wz3/v2_synth.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void number(std::ostream& out,std::uint32_t value,unsigned bytes=4) {
  for(unsigned i=0;i<bytes;++i)out.put(char(value>>(8*i)));
}
void wave(const std::filesystem::path& path,const wz3::V2Module& module,std::uint64_t frames) {
  if(!frames || frames>44100ull*1800)throw std::runtime_error("invalid V2 render duration");
  wz3::V2Synthesis synth(module);std::ofstream out(path,std::ios::binary);
  if(!out)throw std::runtime_error("cannot create WAV output");
  out.write("RIFF",4);number(out,48+frames*8);out.write("WAVEfmt ",8);number(out,16);
  number(out,3,2);number(out,2,2);number(out,44100);number(out,44100*8);number(out,8,2);number(out,32,2);
  out.write("fact",4);number(out,4);number(out,frames);out.write("data",4);number(out,frames*8);
  std::array<float,8192> buffer{};
  while(synth.position()<frames) {
    const auto count=std::min<std::uint64_t>(4096,frames-synth.position());auto samples=std::span(buffer).first(2*count);
    synth.render(samples);
    for(auto sample:samples)number(out,std::bit_cast<std::uint32_t>(sample));
  }
  if(!out)throw std::runtime_error("cannot write WAV output");
}
}
int main(int argc,char** argv) {
  if(argc<2) {std::cerr<<"Usage: wz3-v2m SONG.v2m [--render OUTPUT.wav] [--duration SECONDS]\n";return 2;}
  try {
    std::filesystem::path output;double duration=0;
    for(int i=2;i<argc;++i) {
      const std::string_view option=argv[i];
      if(option=="--render" && i+1<argc)output=argv[++i];
      else if(option=="--duration" && i+1<argc) {
        const std::string_view text=argv[++i];const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),duration);
        if(error!=std::errc{} || end!=text.data()+text.size() || !std::isfinite(duration) || duration<=0 || duration>1800)
          throw std::runtime_error("duration must be in (0,1800] seconds");
      }else throw std::runtime_error("unknown or incomplete V2M option");
    }
    if(duration && output.empty())throw std::runtime_error("--duration requires --render");
    const auto module=wz3::read_v2m(argv[1]);const auto sequence=wz3::sequence_v2m(module);
    const auto frames=duration ? std::uint64_t(std::ceil(duration*44100)) : sequence.end_sample;
    if(!output.empty())wave(output,module,frames);
    std::size_t events=0,notes=0;
    for(const auto& channel:module.channels)for(const auto& event:channel) {
      ++events;if((event.midi[0]&0xf0)==0x90)++notes;
    }
    std::cout<<"{\n  \"time_division\": "<<module.time_division<<",\n  \"end_tick\": "<<module.end_tick
      <<",\n  \"global_events\": "<<module.globals.size()<<",\n  \"midi_events\": "<<events<<",\n  \"note_events\": "<<notes
      <<",\n  \"patches\": "<<module.patch_offsets.size()<<",\n  \"global_parameter_bytes\": "<<module.parameters.size()
      <<",\n  \"patch_bytes\": "<<module.patches.size()<<",\n  \"speech_bytes\": "<<module.speech.size()
      <<",\n  \"packets\": "<<sequence.packets.size()<<",\n  \"sample_rate\": 44100,\n  \"end_sample\": "<<sequence.end_sample
      <<",\n  \"synthesis\": "<<(output.empty() ? "false" : "true")
      <<",\n  \"rendered_frames\": "<<(output.empty() ? 0 : frames)<<"\n}\n";
  }catch(const std::exception& e){std::cerr<<"v2m: "<<e.what()<<'\n';return 1;}
}
