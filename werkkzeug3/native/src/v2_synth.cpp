// Adapter distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Included V2 implementation is public domain; see v2/LICENSE.txt.
#include "wz3/v2_synth.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include "../../../v2/v2defs.h"
#include "../../../v2/synth_core.cpp"

namespace wz3 {
namespace {
void require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
std::vector<std::uint32_t> patch_bank(const V2Module& module) {
  static_assert(offsetof(V2Sound,modmatrix)==90);
  require(V2::nParams==89 && V2::nGParams==23,"unexpected V2 parameter definitions");
  require(module.parameters.size()==23,"historical V2 global parameters need conversion before synthesis");
  require(!module.patch_offsets.empty() && module.patch_offsets.size()<=128,"invalid V2 patch bank");
  for(unsigned i=0;i<module.parameters.size();++i)
    require(module.parameters[i]>=V2::GParams[i].min && module.parameters[i]<=V2::GParams[i].max,"V2 global parameter outside its range");
  // Normalize to a complete, aligned 128-entry table. Unused program slots
  // point at patch zero, as do the synth's inactive channels at initialization.
  std::vector<std::uint8_t> bytes(512);
  for(unsigned i=0;i<128;++i) {
    const auto patch=module.patch_offsets[i<module.patch_offsets.size() ? i : 0];
    require(patch<=module.patches.size() && module.patches.size()-patch>=90,"truncated V2 patch");
    const auto* data=module.patches.data()+patch;
    const auto length=90u+3u*data[89];
    require(length<=module.patches.size()-patch,"truncated V2 modulation matrix");
    if(i<module.patch_offsets.size()) {
      // Reject old layouts even when enough bytes from the next patch follow.
      auto end=module.patches.size();
      for(auto other:module.patch_offsets)if(other>patch)end=std::min(end,std::size_t(other));
      require(length==end-patch,"historical V2 patch layout needs conversion before synthesis");
    }
    for(unsigned p=0;p<89;++p)
      require(data[p]>=V2::Params[p].min && data[p]<=V2::Params[p].max,"V2 patch parameter outside its range");
    for(unsigned mod=0;mod<data[89];++mod) {
      const auto* entry=data+90+3*mod;
      require(entry[0]<V2::nModSources && entry[1]<=128 && entry[2]<88,"invalid V2 modulation route");
      require(V2::Params[entry[2]].isdest,"V2 modulation targets a discrete parameter");
    }
    const auto offset=std::uint32_t(bytes.size());
    for(unsigned b=0;b<4;++b)bytes[4*i+b]=std::uint8_t(offset>>(8*b));
    bytes.insert(bytes.end(),data,data+length);
  }
  std::vector<std::uint32_t> aligned((bytes.size()+3)/4);
  std::memcpy(aligned.data(),bytes.data(),bytes.size());return aligned;
}
}
struct V2Synthesis::Impl {
  std::vector<std::uint32_t> patches;
  V2Sequence sequence;
  std::unique_ptr<V2Synth> synth;
  std::size_t packet=0;
  std::uint64_t sample=0;
  unsigned rate;
  Impl(const V2Module& module,unsigned sample_rate,std::uint32_t seed)
    :patches(patch_bank(module)),sequence(sequence_v2m(module,sample_rate)),synth(std::make_unique<V2Synth>()),rate(sample_rate) {
    require(rate>=8000 && rate<=96000,"V2 synthesis supports 8..96 kHz");
    // The archived Werkkzeug3 build disables Ronan. Do not silently drop
    // speech from other modules when they actually use the reserved channel.
    require(module.channels[15].empty() || module.speech.size()<=4,"Ronan speech synthesis is not ported");
    synth->init(patches.data(),rate);synth->setGlobals(module.parameters.data());
    // The original assembler seeded oscillators/LFOs from RDTSC. Give native
    // renders explicit, reproducible seeds independent of the process RNG.
    for(auto& voice:synth->voicesw) {
      for(auto& osc:voice.osc)osc.nseed=urandom(&seed);
      for(auto& lfo:voice.lfo)lfo.nseed=urandom(&seed);
    }
  }
};
V2Synthesis::V2Synthesis(const V2Module& module,unsigned rate,std::uint32_t seed)
  :impl(std::make_unique<Impl>(module,rate,seed)) {}
V2Synthesis::~V2Synthesis()=default;
std::uint64_t V2Synthesis::position() const {return impl->sample;}
void V2Synthesis::render(std::span<float> output) {
  require(output.size()%2==0,"V2 output must contain complete stereo frames");
  auto& state=*impl;const auto frames=output.size()/2;
  require(frames<=std::uint64_t(state.rate)*1800-state.sample,"V2 synthesis exceeds 30 minutes");
  std::size_t done=0;
  while(done<frames) {
    while(state.packet<state.sequence.packets.size() && state.sequence.packets[state.packet].sample==state.sample)
      state.synth->processMIDI(state.sequence.packets[state.packet++].midi.data());
    auto count=std::min<std::uint64_t>(frames-done,4096);
    if(state.packet<state.sequence.packets.size())count=std::min(count,state.sequence.packets[state.packet].sample-state.sample);
    require(count!=0,"unordered V2 sample sequence");
    state.synth->render(output.data()+2*done,int(count),nullptr,false);
    state.sample+=count;done+=count;
  }
  for(auto sample:output)require(std::isfinite(sample),"V2 generated a non-finite sample");
}
}
