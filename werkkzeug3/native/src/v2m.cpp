// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Format/order/clock reference: CV2MPlayer in ../_viruz2.cpp.
#include "wz3/v2m.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace wz3 {
namespace {
constexpr std::size_t max_bytes=16*1024*1024;
void require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
std::uint32_t word(std::span<const std::uint8_t> bytes,std::size_t offset=0) {
  require(offset<=bytes.size() && bytes.size()-offset>=4,"truncated V2M word");
  return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset+1])<<8 |
    std::uint32_t(bytes[offset+2])<<16 | std::uint32_t(bytes[offset+3])<<24;
}
class Reader {
  std::span<const std::uint8_t> bytes;
public:
  explicit Reader(std::span<const std::uint8_t> input):bytes(input) {}
  std::size_t remaining() const {return bytes.size();}
  std::span<const std::uint8_t> take(std::size_t count) {
    require(count<=bytes.size(),"truncated V2M section");
    const auto result=bytes.first(count);bytes=bytes.subspan(count);return result;
  }
  std::uint32_t number() {return word(take(4));}
  std::vector<std::uint8_t> blob(std::size_t maximum) {
    const auto count=number();require(count<=maximum,"V2M section exceeds limit");
    const auto result=take(count);return {result.begin(),result.end()};
  }
};
std::uint32_t delta(std::span<const std::uint8_t> data,std::size_t count,std::size_t index) {
  return std::uint32_t(data[index]) | std::uint32_t(data[count+index])<<8 |
    std::uint32_t(data[2*count+index])<<16;
}
void advance_tick(std::uint32_t& tick,std::uint32_t change,std::uint32_t end) {
  require(tick<=end && change<=end-tick,"V2M event exceeds saved endpoint");tick+=change;
}
unsigned order(const V2Event& event) {
  const auto type=event.midi[0]&0xf0;
  return type==0xc0 ? 0 : type==0xb0 ? event.midi[1] : type==0xe0 ? 8 : 9;
}
void events(Reader& reader,V2Module& module,unsigned channel,std::uint32_t count,
            unsigned status,unsigned controller=0) {
  const unsigned columns=status==0x90 || status==0xe0 ? 5 : 4;
  require(count<=reader.remaining()/columns,"invalid V2M event count");
  const auto data=reader.take(std::size_t(count)*columns);
  std::uint32_t tick=0,pass=0;std::uint8_t first=0,second=0;
  auto& output=module.channels[channel];
  for(std::size_t i=0;i<count;++i) {
    const auto change=delta(data,count,i);advance_tick(tick,change,module.end_tick);
    pass=change || !i ? 0 : pass+1;
    first=std::uint8_t(first+data[3*count+i]);
    if(columns==5)second=std::uint8_t(second+data[4*count+i]);
    require(first<128 && second<128,"invalid V2M MIDI value");
    V2Event event{tick,status==0x90 ? 0 : pass,
      {std::uint8_t(status|channel),first,second},status==0xc0 ? 2u : 3u};
    if(status==0xb0) {event.midi[1]=std::uint8_t(controller);event.midi[2]=first;}
    output.push_back(event);
  }
}
}

V2Module parse_v2m(std::span<const std::uint8_t> data) {
  require(data.size()<=max_bytes,"V2M exceeds 16 MiB limit");Reader reader(data);V2Module module;
  module.time_division=reader.number();module.end_tick=reader.number();
  require(module.time_division && module.time_division<=100000,"invalid V2M time division");
  require(module.end_tick && module.end_tick<0x80000000u,"invalid V2M endpoint");
  const auto count=reader.number();require(count<=reader.remaining()/10,"invalid V2M global event count");
  const auto globals=reader.take(std::size_t(count)*10);std::uint32_t tick=0,pass=0;
  for(std::size_t i=0;i<count;++i) {
    const auto change=delta(globals,count,i);advance_tick(tick,change,module.end_tick);
    pass=change || !i ? 0 : pass+1;
    V2Global global{tick,pass,word(globals,3*count+4*i),globals[7*count+i],globals[8*count+i],globals[9*count+i]};
    require(global.microseconds && global.microseconds<=9000000,"invalid V2M tempo");
    require(global.numerator && global.denominator && global.ticks_per_quarter,"invalid V2M time signature");
    module.globals.push_back(global);
  }
  for(unsigned channel=0;channel<16;++channel) {
    const auto notes=reader.number();if(!notes)continue;
    events(reader,module,channel,notes,0x90);
    events(reader,module,channel,reader.number(),0xc0);
    events(reader,module,channel,reader.number(),0xe0);
    for(unsigned controller=1;controller<=7;++controller)
      events(reader,module,channel,reader.number(),0xb0,controller);
    auto& list=module.channels[channel];
    std::stable_sort(list.begin(),list.end(),[](const auto& a,const auto& b) {
      return std::tuple(a.tick,a.pass,order(a))<std::tuple(b.tick,b.pass,order(b));
    });
  }
  module.parameters=reader.blob(16384);module.patches=reader.blob(1048576);
  require(!module.parameters.empty(),"V2M has no global parameters");
  const auto table_bytes=word(module.patches);
  require(table_bytes && table_bytes%4==0 && table_bytes<=512 && table_bytes<module.patches.size(),"invalid V2M patch table");
  for(unsigned offset=0;offset<table_bytes;offset+=4) {
    const auto patch=word(module.patches,offset);
    require(patch>=table_bytes && patch<module.patches.size(),"invalid V2M patch offset");
    module.patch_offsets.push_back(patch);
  }
  for(const auto& channel:module.channels)for(const auto& event:channel)
    if((event.midi[0]&0xf0)==0xc0)require(event.midi[1]<module.patch_offsets.size(),"V2M program has no patch");
  if(reader.remaining())module.speech=reader.blob(8191);
  if(!module.speech.empty()) {
    const auto strings=word(module.speech);require(strings<=256 && strings<=(module.speech.size()-4)/4,"invalid V2M speech count");
    for(unsigned i=0;i<strings;++i) {
      const auto start=word(module.speech,4+4*i);
      require(start>=4+4*strings && start<module.speech.size(),"invalid V2M speech offset");
      require(std::find(module.speech.begin()+start,module.speech.end(),0)!=module.speech.end(),"unterminated V2M speech text");
    }
  }
  // The archived converted test modules have three (Patient Zero) or four
  // (Zeitmaschine) zero bytes after the declared speech section.
  require(reader.remaining()<=4,"unexpected trailing V2M data");
  for(auto value:reader.take(reader.remaining()))require(value==0,"nonzero V2M padding");
  return module;
}

V2Module read_v2m(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary|std::ios::ate);
  require(bool(input),"cannot open V2M music");const auto size=input.tellg();
  require(size>=0 && size<=std::streamoff(max_bytes),"V2M exceeds 16 MiB limit");
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));input.seekg(0);
  require(bool(input.read(reinterpret_cast<char*>(data.data()),size)),"cannot read V2M music");return parse_v2m(data);
}

V2Sequence sequence_v2m(const V2Module& module,unsigned rate) {
  require(rate>=8000 && rate<=192000,"invalid V2M sample rate");
  require(module.time_division && module.time_division<=100000,"invalid V2M time division");
  std::array<std::size_t,16> cursors{};std::size_t global=0;V2Sequence result;
  std::uint32_t tick=0,remainder=0;std::uint64_t sample=0,usecs=5000ull*rate;
  const std::uint64_t divisor=10000ull*module.time_division;
  const auto advance=[&](std::uint32_t next) {
    require(next>=tick,"unordered V2M events");
    const auto product=std::uint64_t(next-tick)*usecs;
    const auto carry=std::uint64_t(remainder)+product%divisor;
    sample+=product/divisor+(carry>>32);remainder=std::uint32_t(carry);tick=next;
    require(sample<=std::uint64_t(rate)*1800,"V2M exceeds 30 minute playback limit");
  };
  for(;;) {
    auto next=std::pair(std::numeric_limits<std::uint32_t>::max(),0u);
    if(global<module.globals.size())next={module.globals[global].tick,module.globals[global].pass};
    for(unsigned ch=0;ch<16;++ch)if(cursors[ch]<module.channels[ch].size()) {
      const auto& event=module.channels[ch][cursors[ch]];next=std::min(next,std::pair(event.tick,event.pass));
    }
    if(next.first==std::numeric_limits<std::uint32_t>::max())break;
    // Reset initializes nexttime to the first event, and the first Tick is
    // called immediately, even if that event carries a nonzero saved tick.
    if(result.packets.empty())tick=next.first;
    advance(next.first);V2Packet packet{sample,tick,next.second,{}};
    if(global<module.globals.size()) {
      const auto& tempo=module.globals[global];
      if(std::pair(tempo.tick,tempo.pass)==next) {
        usecs=std::uint64_t(tempo.microseconds)*(rate/100);
        require(usecs<=std::numeric_limits<std::uint32_t>::max(),"V2M tempo overflows original clock");++global;
      }
    }
    for(unsigned ch=0;ch<16;++ch)while(cursors[ch]<module.channels[ch].size()) {
      const auto& event=module.channels[ch][cursors[ch]];
      if(std::pair(event.tick,event.pass)!=next)break;
      packet.midi.insert(packet.midi.end(),event.midi.begin(),event.midi.begin()+event.length);++cursors[ch];
    }
    packet.midi.push_back(0xfd);result.packets.push_back(std::move(packet));
  }
  advance(module.end_tick);result.end_sample=sample;return result;
}
} // namespace wz3
