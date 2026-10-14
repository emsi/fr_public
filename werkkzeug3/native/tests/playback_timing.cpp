// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/playback_timing.hpp"
#include "frame_fixture.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void timing(const wz3::Project& project) {
  wz3::PlaybackTiming theta(project,44100);
  check(theta.beat(44100*60)==138*65536,"Theta minute-to-beat conversion");
  check(theta.milliseconds(44100)==1000,"sample-to-millisecond conversion");
  check(theta.end_frames()==11676914,"Theta saved beat-609 endpoint");
  check(theta.beat(theta.end_frames())>=609*65536 && theta.beat(theta.end_frames()-1)<609*65536,"endpoint must be the first sample reaching beat 609");
  auto buzz=project;buzz.settings[9]=1;wz3::PlaybackTiming timing(buzz,44100);
  for(std::uint64_t sample=0;sample<44100*300;sample+=12345) {
    const auto original=(sample*65536/(60*44100/(138*8)))/8;
    check(timing.beat(sample)==std::int32_t(original),"Buzz integer tick formula changed");
  }
  check(timing.beat(timing.end_frames())>=609*65536 && timing.beat(timing.end_frames()-1)<609*65536,"Buzz endpoint rounding");
  wz3::PlaybackTiming resampled(buzz,48000);
  check(resampled.beat(48000)==timing.beat(44100),"Buzz reference sample rate");
  auto release=buzz;release.release_timing=true;release.settings[6]=592u<<16;
  const wz3::PlaybackTiming original(release,44100);
  // Expected values come from the restored 2005 executable's x86 fragment
  // at 0x824583, independently executed by release_audit.clock_oracle.
  constexpr std::array<std::array<std::uint64_t,2>,11> vectors{{
    {0,0},{1,3},{2396,8192},{44100,150779},{1323000,4523378},{5292000,18093515},
    {8599852,29403166},{9702000,33171445},{11025000,37694824},
    {11347456,38797312},{11347457,38797315}}};
  for(const auto& pair:vectors) {
    check(original.beat(pair[0])==std::int32_t(pair[1]),"released x86 clock mismatch");
    check(original.samples_at_beat(std::int32_t(pair[1]))==pair[0],"released inverse sample clock mismatch");
  }
  check(original.end_frames()==11347457 && original.beat(original.end_frames()-1)==592*65536 &&
        original.beat(original.end_frames())>592*65536,"released strict exit condition");
  const wz3::PlaybackTiming converted(release,48000);
  check(converted.beat(converted.end_frames())>592*65536 && converted.beat(converted.end_frames()-1)<=592*65536,
        "resampled release endpoint");
  for(unsigned rate:{8000,44100,48000,192000})for(unsigned buzz_flag:{0,1}) {
    auto p=project;p.settings[9]=buzz_flag;const wz3::PlaybackTiming clock(p,rate);
    for(std::int32_t beat=0;beat<600*65536;beat+=12347) {
      const auto sample=clock.samples_at_beat(beat);
      check(clock.beat(sample)>=beat && (!sample || clock.beat(sample-1)<beat),"inverse clock must find the first reaching sample");
    }
  }
  auto no_endpoint=project;no_endpoint.settings[6]=0;
  const wz3::PlaybackTiming clock(no_endpoint,44100);check(clock.samples_at_beat(138*65536)==44100*60,"offline clock needs no endpoint");
  bool caught=false;try {clock.end_frames();}catch(const std::runtime_error&){caught=true;}check(caught,"playback must reject a missing endpoint");
  caught=false;try {clock.samples_at_beat(-1);}catch(const std::runtime_error&){caught=true;}check(caught,"negative beat accepted");
  FrameFixture x;x.p.settings[4]=f(138);x.p.settings[6]=592u<<16;x.p.settings[9]=1;x.p.release_timing=true;
  const auto root=x.add(0xd);wz3::FramePlayer player(x.p,x.g);
  check(player.frame(root,29403166,64,64).milliseconds==195007,"frame default clock differs from released samples");
  check(player.frame(root,29403166,64,64,1234).milliseconds==1234,"explicit frame time was not retained");
}
}
int main(int argc,char** argv) {
  try {check(argc==2,"expected Theta project path");timing(wz3::read_project(argv[1]));}
  catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
