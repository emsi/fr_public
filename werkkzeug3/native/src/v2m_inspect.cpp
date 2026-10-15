// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/v2m.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
  if(argc!=2) {std::cerr<<"Usage: wz3-v2m SONG.v2m\n";return 2;}
  try {
    const auto module=wz3::read_v2m(argv[1]);const auto sequence=wz3::sequence_v2m(module);
    std::size_t events=0,notes=0;
    for(const auto& channel:module.channels)for(const auto& event:channel) {
      ++events;if((event.midi[0]&0xf0)==0x90)++notes;
    }
    std::cout<<"{\n  \"time_division\": "<<module.time_division<<",\n  \"end_tick\": "<<module.end_tick
      <<",\n  \"global_events\": "<<module.globals.size()<<",\n  \"midi_events\": "<<events<<",\n  \"note_events\": "<<notes
      <<",\n  \"patches\": "<<module.patch_offsets.size()<<",\n  \"global_parameter_bytes\": "<<module.parameters.size()
      <<",\n  \"patch_bytes\": "<<module.patches.size()<<",\n  \"speech_bytes\": "<<module.speech.size()
      <<",\n  \"packets\": "<<sequence.packets.size()<<",\n  \"sample_rate\": 44100,\n  \"end_sample\": "<<sequence.end_sample
      <<",\n  \"synthesis\": false\n}\n";
  }catch(const std::exception& e){std::cerr<<"v2m: "<<e.what()<<'\n';return 1;}
}
