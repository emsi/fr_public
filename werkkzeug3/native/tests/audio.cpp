// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/audio.hpp"
#include "audio_sdl.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void callback_clock() {
  wz3::PcmAudio pcm{8000,2,{.1f,.2f,.3f,.4f,.5f,.6f}};
  wz3::AudioPlayback player(pcm);std::array<float,4> block{};
  check(player.position(900000000)==0,"clock moved before the first callback");
  player.fill(block,1000000000);check(block==std::array<float,4>{.1f,.2f,.3f,.4f},"stereo PCM order");
  check(player.position(1000125000)==1,"sample clock interpolation");
  check(player.position(2000000000)==2,"clock must stop at a stalled buffer boundary");
  player.fill(block,2000000000);check(block==std::array<float,4>{.5f,.6f,0,0},"EOF must be padded with silence");
  check(player.position(2000125000)==3,"callback clock continuity");
  player.fill(block,2000250000);check(std::all_of(block.begin(),block.end(),[](float v){return v==0;}),"silence after EOF");
  check(player.position(2000500000)==6,"clock must continue after short music ends");
  wz3::AudioPlayback mute(pcm,true);mute.fill(block,1000000000);
  check(block==std::array<float,4>{} && mute.position(2000000000)==2,"mute must preserve sample timing");
}
void device(const wz3::PcmAudio& pcm) {
  check(SDL_Init(SDL_INIT_AUDIO)==0,SDL_GetError());
  try {
    wz3::SdlAudio audio(pcm);check(audio.position()==0,"device must start paused");audio.start();
    const auto deadline=SDL_GetTicks64()+2000;std::uint64_t previous=0;
    while(audio.position()<pcm.rate/5 && SDL_GetTicks64()<deadline) {
      const auto position=audio.position();check(position>=previous,"SDL clock went backwards");previous=position;SDL_Delay(1);
    }
    check(audio.position()>=pcm.rate/5 && audio.callback_count()>1,"SDL callbacks did not drive the clock");
  }catch(...){SDL_Quit();throw;}
  SDL_Quit();
}
void capture(const wz3::PcmAudio& pcm,const char* path) {
  std::ifstream input(path,std::ios::binary|std::ios::ate);check(bool(input),"cannot read SDL disk capture");
  const auto bytes=input.tellg();check(bytes>0 && bytes<=512*1024*1024 && bytes%sizeof(float)==0,"invalid PCM capture length");
  std::vector<float> actual(std::size_t(bytes)/sizeof(float));input.seekg(0);
  check(bool(input.read(reinterpret_cast<char*>(actual.data()),bytes)),"truncated PCM capture");
  // SDL can write silent buffers while the device is paused for initial loading.
  const auto source=std::find_if(pcm.samples.begin(),pcm.samples.end(),[](float v){return v!=0;});
  const auto sound=std::find_if(actual.begin(),actual.end(),[](float v){return v!=0;});
  check(source!=pcm.samples.end() && sound!=actual.end(),"silent PCM capture");
  const auto offset=(sound-actual.begin())-(source-pcm.samples.begin());
  check(offset>=0 && offset%pcm.channels==0,"invalid initial SDL silence");
  check(actual.size()-offset>=pcm.samples.size(),"SDL capture misses the end of the music");
  check(std::equal(pcm.samples.begin(),pcm.samples.end(),actual.begin()+offset),"SDL output differs from decoded Theta PCM");
  check(std::all_of(actual.begin()+offset+pcm.samples.size(),actual.end(),[](float v){return v==0;}),"non-silent demo tail");
  std::cout<<"SDL disk output matches all "<<pcm.frames()<<" decoded stereo frames; "<<offset/pcm.channels<<" initial silent frames, "
    <<(actual.size()-offset-pcm.samples.size())/pcm.channels<<" tail frames\n";
}
}
int main(int argc,char** argv) {
  try {
    check(argc==3 || argc==4,"expected project and Ogg paths, optionally full SDL disk capture");callback_clock();
    const auto pcm=wz3::read_vorbis(argv[2]);
    check(pcm.rate==44100 && pcm.channels==2,"Theta PCM format");
    check(pcm.frames()>44100*250 && pcm.frames()<44100*280,"Theta decoded duration");
    double energy=0;for(float v:pcm.samples)energy+=v*v;
    check(energy/pcm.samples.size()>.001,"decoded music is silent");
    if(argc==4)capture(pcm,argv[3]);
    device(pcm);
    std::cout<<pcm.frames()<<" stereo frames, "<<double(pcm.frames())/pcm.rate<<" s, RMS "<<std::sqrt(energy/pcm.samples.size())<<"; clock and SDL callback checks passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
