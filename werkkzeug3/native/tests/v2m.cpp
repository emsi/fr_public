// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/v2m.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace wz3 {std::vector<V2Packet> original_v2_packets(std::span<const std::uint8_t>,unsigned);}
namespace {
void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action) {
  bool rejected=false;try {action();}catch(const std::runtime_error&) {rejected=true;}
  check(rejected,"invalid V2M accepted");
}
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);check(bool(input),"fixture missing");
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void put(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)bytes.push_back(std::uint8_t(value>>(8*i)));
}
void patch(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)bytes.at(offset+i)=std::uint8_t(value>>(8*i));
}
void compare(std::span<const std::uint8_t> bytes,unsigned rate) {
  const auto module=wz3::parse_v2m(bytes);const auto actual=wz3::sequence_v2m(module,rate);
  const auto expected=wz3::original_v2_packets(bytes,rate);
  check(actual.packets.size()==expected.size(),"original V2 packet count differs");
  for(std::size_t i=0;i<expected.size();++i) {
    if(actual.packets[i].sample!=expected[i].sample || actual.packets[i].midi!=expected[i].midi)
      throw std::runtime_error("original V2 scheduling differs at packet "+std::to_string(i));
  }
}
std::vector<std::uint8_t> fixture() {
  std::vector<std::uint8_t> bytes;
  put(bytes,128);put(bytes,512);put(bytes,2);
  // Two global events at ticks 0,128. Values are absolute, time is deltas
  // split across three byte planes, and tempo words may be unaligned.
  bytes.insert(bytes.end(),{0,128,0,0,0,0});put(bytes,500000);put(bytes,250000);
  bytes.insert(bytes.end(),{4,4,4,4,8,8});
  put(bytes,3);
  // Notes: tick0 note60 vel100, tick0 note64 vel100, tick256 note60 vel0.
  bytes.insert(bytes.end(),{0,0,0, 0,0,1, 0,0,0, 60,4,252, 100,0,156});
  put(bytes,2);bytes.insert(bytes.end(),{0,0,0,0,0,0,0,1}); // two tick0 programs
  put(bytes,0); // no pitch bend; archived player has a separate pitch-stride defect
  put(bytes,2);bytes.insert(bytes.end(),{0,0,0,0,0,0,1,1}); // two tick0 CC1 changes
  for(unsigned i=0;i<6;++i)put(bytes,0);
  for(unsigned i=1;i<16;++i)put(bytes,0);
  put(bytes,1);bytes.push_back(0); // raw global parameters, synthesis not requested
  put(bytes,10);put(bytes,8);put(bytes,9);bytes.insert(bytes.end(),{0,0});
  put(bytes,0);return bytes;
}
void synthetic() {
  const auto bytes=fixture();const auto module=wz3::parse_v2m(bytes);
  const auto sequence=wz3::sequence_v2m(module);
  check(sequence.packets.size()==4,"fixture packet count");
  check(sequence.packets[0].midi==std::vector<std::uint8_t>({0xc0,0,0xb0,1,1,0x90,60,100,0x90,64,100,0xfd}),"program/controller must precede notes");
  check(sequence.packets[1].midi==std::vector<std::uint8_t>({0xc0,1,0xb0,1,2,0xfd}),"same-tick controllers must retain repeat passes");
  check(sequence.packets[2].sample==22050 && sequence.packets[3].sample==33075 && sequence.end_sample==55125,"tempo change sample positions");
  compare(bytes,44100);compare(bytes,48000);
  for(auto size:{0u,4u,12u,17u,36u,60u,100u})rejects([&]{wz3::parse_v2m(std::span(bytes).first(size));});
  for(auto [offset,value]:{std::pair(0u,0u),{4u,1u},{8u,0xffffffffu},{18u,0u}}) {
    auto damaged=bytes;patch(damaged,offset,value);rejects([&]{wz3::parse_v2m(damaged);});
  }
  auto damaged=bytes;damaged.push_back(1);rejects([&]{wz3::parse_v2m(damaged);});
  rejects([&]{wz3::sequence_v2m(module,0);});
}
}
int main(int argc,char** argv) {
  try {
    check(argc==2,"expected archive root");synthetic();const std::filesystem::path root=argv[1];
    for(const auto* name:{"werkkzeug3/data/debris/debris_ost.v2m","werkkzeug3/data/debris/debrisfx_beta_edited.v2m",
                         "v2/v2m/pzero_new.v2m","v2/v2m/v2_zeitmaschine_new.v2m","werkkzeug3_kkrieger/data/kkrieger6.v2m"}) {
      const auto bytes=read(root/name);compare(bytes,44100);compare(bytes,48000);
      for(std::size_t size=12;size+16<bytes.size();size+=1537)
        rejects([&]{wz3::parse_v2m(std::span(bytes).first(size));});
      const auto module=wz3::parse_v2m(bytes);
      std::cout<<name<<": "<<wz3::sequence_v2m(module).packets.size()<<" packets match the archived scheduler at 44.1/48 kHz\n";
    }
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
