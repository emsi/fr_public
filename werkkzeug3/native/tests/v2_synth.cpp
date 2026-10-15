// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/v2_synth.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action) {
  bool rejected=false;try {action();}catch(const std::runtime_error&) {rejected=true;}
  check(rejected,"invalid synthesis request accepted");
}
std::vector<float> render(const wz3::V2Module& module,unsigned rate,unsigned seconds,bool fragmented,std::uint32_t seed=1) {
  wz3::V2Synthesis synth(module,rate,seed);std::vector<float> output(std::size_t(rate)*seconds*2);
  if(!fragmented)synth.render(output);
  else {
    const std::array<unsigned,6> sizes{1,17,127,511,1024,4096};std::size_t pos=0,index=0;
    while(pos<output.size()) {
      const auto count=std::min(output.size()-pos,std::size_t(sizes[index++%sizes.size()])*2);
      synth.render(std::span(output).subspan(pos,count));pos+=count;
    }
  }
  check(synth.position()==std::uint64_t(rate)*seconds,"synth sample position differs");
  double energy=0,peak=0;
  for(float sample:output) {
    check(std::isfinite(sample),"non-finite synthesized sample");
    energy+=double(sample)*sample;peak=std::max(peak,std::abs(double(sample)));
  }
  check(energy/output.size()>1e-7 && peak<4,"silent or unstable synthesis");
  std::cout<<rate<<" Hz, "<<seconds<<" s: RMS "<<std::sqrt(energy/output.size())<<", peak "<<peak<<'\n';return output;
}
void invalid(const wz3::V2Module& original) {
  for(auto rate:{0u,7999u,96001u,192000u})rejects([&]{wz3::V2Synthesis synth(original,rate);});
  wz3::V2Synthesis synth(original);std::array<float,3> odd{};
  rejects([&]{synth.render(odd);});synth.render({});check(synth.position()==0,"empty render advanced the clock");
  auto module=original;module.parameters.pop_back();rejects([&]{wz3::V2Synthesis s(module);});
  module=original;module.parameters[0]=255;rejects([&]{wz3::V2Synthesis s(module);});
  module=original;module.patches.pop_back();rejects([&]{wz3::V2Synthesis s(module);});
  module=original;module.patch_offsets[0]=module.patches.size();rejects([&]{wz3::V2Synthesis s(module);});
  module=original;module.patches[module.patch_offsets[0]+2]=255;rejects([&]{wz3::V2Synthesis s(module);});
  for(auto offset:original.patch_offsets)if(original.patches[offset+89]) {
    module=original;module.patches[offset+90]=255;rejects([&]{wz3::V2Synthesis s(module);});
    module=original;module.patches[offset+92]=2;rejects([&]{wz3::V2Synthesis s(module);});break;
  }
  module=original;module.channels[15].push_back({0,0,{0x9f,60,100},3});
  rejects([&]{wz3::V2Synthesis s(module);});
}
}
int main(int argc,char** argv) {
  try {
    check(argc==2,"expected archive root");const std::filesystem::path root=argv[1];
    const auto ost=wz3::read_v2m(root/"werkkzeug3/data/debris/debris_ost.v2m");invalid(ost);
    const auto whole=render(ost,44100,8,false);
    check(whole==render(ost,44100,8,true),"audio depends on render buffer boundaries or another synth instance");
    check(whole!=render(ost,44100,8,false,2),"noise seed has no effect");
    for(auto rate:{8000u,48000u,96000u})render(ost,rate,2,true);
    const auto fx=wz3::read_v2m(root/"werkkzeug3/data/debris/debrisfx_beta_edited.v2m");
    check(render(fx,44100,8,false)==render(fx,44100,8,true),"effects synthesis depends on block size");
    const auto old=wz3::read_v2m(root/"werkkzeug3_kkrieger/data/kkrieger6.v2m");
    rejects([&]{wz3::V2Synthesis synth(old);});
    std::cout<<"Debris OST/effects synthesis, sample rates, repeatability and malformed patches checked\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
