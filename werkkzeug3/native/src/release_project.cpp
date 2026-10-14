// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// The final 1.01 graph uses the KDoc::Init layout with one-byte class IDs.
// Packed floats follow sReadF16/F24/X16 in ../_types.cpp.
#include "wz3/project.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace wz3 {
namespace {
struct Reader {
  std::span<const std::uint8_t> bytes; std::size_t at{};
  [[noreturn]] void fail(const char* message) const {
    throw std::runtime_error("packed graph at byte "+std::to_string(at)+": "+message);
  }
  void require(std::size_t count) const {if(count>bytes.size()-at)fail("truncated input");}
  unsigned integer(unsigned count) {
    require(count);unsigned value=0;
    for(unsigned i=0;i<count;++i)value|=unsigned(bytes[at++])<<(8*i);
    return value;
  }
  unsigned byte(){return integer(1);}
  unsigned word(){return integer(4);}
  unsigned short_value(){const auto v=byte();return v<128 ? v : (v&127)|(byte()<<7);}
  void skip(std::size_t count){require(count);at+=count;}
  std::string string() {
    const auto start=at;
    while(byte())if(at-start>16383)fail("string exceeds format limit");
    return std::string(reinterpret_cast<const char*>(bytes.data()+start),at-start-1);
  }
  std::uint32_t f16() {
    auto v=byte();
    if(v==0)return 0;
    if(v==128)return 0x3f800000;
    if(v==1)return 0x3f000000;
    if(v==129)return 0x3e800000;
    v=(v<<8)|byte();return ((v&32768)<<16)|((((v>>10)&31)+112)<<23)|((v&1023)<<13);
  }
  std::uint32_t f24() {
    const auto high=byte();
    if(high==0)return 0;
    if(high==1)return 0x3f800000;
    if(high==255)return 0xbf800000;
    const auto low=integer(2);return (high<<23)|((low&32768)<<16)|((low&32767)<<8);
  }
  std::uint32_t parameter(char c) {
    if(c>='A' && c<='Z')c+=32;
    switch(c) {
      case '-':return 0;
      case 'g':return f16();case 'f':return f24();
      case 'e':return std::bit_cast<std::uint32_t>(float(std::bit_cast<std::int16_t>(std::uint16_t(integer(2))))/4096.f);
      case 'i':case 'c':return word();
      case 's':return std::uint32_t(std::int32_t(std::bit_cast<std::int16_t>(std::uint16_t(integer(2)))));
      case 'b':return byte();case 'm':return integer(3);
      default:fail("unknown parameter packing");
    }
  }
  std::string reference(unsigned value,unsigned count,std::string_view prefix) {
    if(!value)return {};
    if(value>count)fail("invalid packed reference");
    return std::string(prefix)+std::to_string(value-1);
  }
};
struct Class {std::uint32_t convention{},command{};std::string packing;};
}
Project parse_release_project(std::vector<std::uint8_t> bytes) {
  if(bytes.size()>64*1024*1024)throw std::runtime_error("runtime project exceeds 64 MiB limit");
  Project p;p.bytes=std::move(bytes);p.runtime_graph=true;p.version=1;
  Reader r{p.bytes};
  constexpr std::array<std::uint8_t,8> magic{'W','Z','3','T','H','R','1',0};
  for(auto value:magic)if(r.byte()!=value)r.fail("incorrect native release envelope");
  p.settings[2]=r.word();p.settings[3]=r.word();p.settings[4]=r.word();p.settings[6]=r.word();p.settings[9]=r.word();
  const auto graph_size=r.word();const auto bpm=as_float(p.settings[4]);
  const auto flags=p.settings[9];p.release_timing=(flags&2)!=0;p.settings[9]=flags&1;
  if(graph_size!=p.bytes.size()-r.at || p.settings[2]<64 || p.settings[2]>4096 ||
     p.settings[3]<64 || p.settings[3]>4096 || !std::isfinite(bpm) || bpm<1 || bpm>1000 ||
     !p.settings[6] || p.settings[6]>0x7fffffff || flags>3 || (p.release_timing && (flags!=3 || bpm!=138.f || p.settings[6]!=(592u<<16))))r.fail("invalid runtime envelope settings/length");
  p.demo_name=p.release_timing ? "fr-038: theta final 1.01" : "fr-038: theta final 1.01 (provisional timing)";p.song_name="theta.ogg";
  p.pages.resize(1);p.pages[0].name="Released runtime graph";
  const auto count=r.short_value(),spline_count=r.short_value();
  if(!count || count>32767 || spline_count>32767)r.fail("invalid graph counts");
  for(auto& root:p.runtime_roots){root=r.short_value();if(root>count)r.fail("invalid root");}
  std::vector<Class> classes;
  while(const auto convention=r.word()) {
    if(classes.size()>=128)r.fail("too many classes");
    Class c{convention,r.byte(),r.string()};
    if(c.packing.size()>255)r.fail("excessive packing string");
    classes.push_back(std::move(c));
  }
  std::vector<unsigned> types;
  for(unsigned i=0;i<count;++i) {
    const auto type=r.byte();if((type&127)>=classes.size())r.fail("invalid operator class");
    const auto& c=classes[type&127];types.push_back(type&127);
    Operator op;op.command=c.command;op.convention=c.convention;op.width=1;op.name="release."+std::to_string(i);
    p.operators.push_back(std::move(op));auto& inputs=p.runtime_inputs.emplace_back();
    const auto input_count=(type&128) ? 1 : (c.convention&0x80000000) ? r.byte() : (c.convention>>8)&15;
    for(unsigned j=0;j<input_count;++j) {
      const auto delta=(type&128) ? 0 : r.short_value();
      if(delta>=i)r.fail("input precedes graph start");
      inputs.push_back(i-1-delta);
    }
  }
  for(auto& op:p.operators)for(unsigned j=0;j<((op.convention>>12)&15);++j)
    op.links.push_back(r.reference(r.short_value(),count,"release."));
  for(std::size_t ci=0;ci<classes.size();++ci)for(unsigned i=0;i<count;++i)if(types[i]==ci) {
    auto& op=p.operators[i];op.file_offset=r.at;
    // The release's 0x95 packs 18 words with a convention count of 17. Keep
    // the complete payload; handler compatibility is checked during evaluation.
    for(auto c:classes[ci].packing)op.parameters.push_back(r.parameter(c));
    op.parameters.resize(std::max(op.parameters.size(),std::size_t(op.convention&255)));
    for(unsigned j=0;j<((op.convention>>16)&7);++j)op.strings.push_back(r.string());
    for(unsigned j=0;j<((op.convention>>20)&7);++j)op.splines.push_back(r.reference(r.integer(2),spline_count,"spline."));
    if(op.convention&0x80000)op.blob_size=r.word();
  }
  constexpr unsigned sizes[]={0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,16,4,4,2,0,0,0,8,2,0,0,0};
  for(auto& op:p.operators) {
    const auto start=r.at;
    for(;;) {
      const auto cmd=r.byte();
      if(cmd>=128)r.skip(1);
      else if(cmd<std::size(sizes))r.skip(sizes[cmd]);
      else r.fail("unknown animation opcode");
      if(r.at-start>4096)r.fail("animation bytecode exceeds limit");
      if(cmd==1)break;
    }
    op.animation_code.assign(p.bytes.begin()+start,p.bytes.begin()+r.at);
  }
  const auto event_count=r.short_value();
  for(unsigned i=0;i<event_count;++i) {
    Event event;event.fields[0]=r.short_value();
    if(event.fields[0]>=count)r.fail("invalid event operator");
    event.name=p.operators[event.fields[0]].name;
    event.fields[1]=r.word();event.fields[2]=r.word();event.fields[4]=r.f24();event.fields[5]=r.f24();event.fields[3]=r.byte();
    for(auto& value:event.transform)value=r.f24();
    event.fields[9]=r.word();event.spline=r.reference(r.short_value(),spline_count,"spline.");
    event.interval[0]=r.f16();event.interval[1]=r.f16();p.events.push_back(std::move(event));
  }
  for(unsigned i=0;i<spline_count;++i) {
    Spline s;s.name="spline."+std::to_string(i);const auto type=r.byte();s.interpolation=type>>3;
    if((type&7)>4 || s.interpolation>2)r.fail("invalid spline layout");
    for(unsigned j=0;j<(type&7);++j) {
      auto& channel=s.channels.emplace_back();const auto keys=r.short_value();
      for(unsigned k=0;k<keys;++k)channel.push_back({std::bit_cast<std::uint32_t>(r.byte()/255.f),r.f16()});
    }
    p.splines.push_back(std::move(s));
  }
  for(auto& op:p.operators) {op.blob_offset=r.at;r.skip(op.blob_size);p.blob_bytes+=op.blob_size;}
  if(r.at!=p.bytes.size())r.fail("trailing graph bytes");
  return p;
}
} // namespace wz3
