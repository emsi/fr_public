"""Compile the archived V2M player's event loop with a recording synth.

Only the four MSVC inline-assembly utility blocks are translated; the original
file parser, cursor updates, event order and Render loop remain the reference.
This audits scheduling, not the oscillator/filter implementation or sound.
"""
from pathlib import Path
import re
import sys

root, output = map(Path, sys.argv[1:])
header = (root / "v2mplayer.h").read_text()
source = (root / "v2mplayer.cpp").read_text()
source = source.replace('#include "v2mplayer.h"', '').replace('#include "libv2.h"', '')
blocks = []


def assembly(match):
    block = match[0]
    blocks.append(block)
    if "[nexttime]" in block:
        return """const sU64 product=sU64(sU32(nexttime-time))*usecs;
        const sU64 sum=sU64(*smplrem)+product%td2;
        *smplrem=sU32(sum);*smpldelta=sU32(product/td2+(sum>>32));"""
    if "[a_time]" in block:
        return "destsmpl=sU32(sS64(sS32(a_time))*sS32(m_samplerate)/sS32(m_tpc));"
    if "[a_fadetime]" in block:
        return "ftsmpls=sU32(sS64(sS32(a_fadetime))*sS32(m_samplerate)/sS32(m_tpc));"
    if "rep stosd" in block:
        return "std::fill_n(a_buffer,2*a_len,0.0f);"
    raise ValueError("unrecognized V2M assembly block")


source = re.sub(r"__asm\s*\{[^}]*\}", assembly, source)
assert len(blocks) == 4 and "__asm" not in source
output.write_text(r'''
// Generated from the archived V2 source; see v2/LICENSE.txt.
#include "wz3/v2m.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>
namespace wz3 { namespace original_v2 {
#define V2TYPES
#define __stdcall
using sU8=std::uint8_t;using sU32=std::uint32_t;using sS32=std::int32_t;
using sU64=std::uint64_t;using sS64=std::int64_t;
using sInt=int;using sBool=int;using sF32=float;using sF64=double;
#define sFALSE 0
#define sTRUE 1
std::uint64_t sample;
std::vector<V2Packet> packets;
void synthInit(void*,const void*,int) {sample=0;packets.clear();}
void synthSetGlobals(void*,const void*) {}
void synthSetLyrics(void*,const char**) {}
void synthRender(void*,void*,int count,void*,int) {sample+=count;}
void synthProcessMIDI(void*,const void* pointer) {
  const auto* data=static_cast<const sU8*>(pointer);sU8 status=0;
  V2Packet packet{sample,0,0,{}};
  while(*data!=0xfd) {
    if(*data&0x80)status=*data++;
    if(status<0x80)throw std::runtime_error("missing original MIDI status");
    packet.midi.push_back(status);packet.midi.push_back(*data++);
    if((status&0xf0)!=0xc0)packet.midi.push_back(*data++);
  }
  packet.midi.push_back(0xfd);packets.push_back(std::move(packet));
}
''' + header + '\n' + source + r'''
}
std::vector<V2Packet> original_v2_packets(std::span<const std::uint8_t> input,unsigned rate) {
  // Original parser has no length parameter and permits a missing speech tail.
  // Only pass fixtures already validated by the native reader, with tail padding.
  auto bytes=std::vector<std::uint8_t>(input.begin(),input.end());bytes.resize(bytes.size()+16);
  auto player=std::make_unique<original_v2::V2MPlayer>();player->Init();
  if(!player->Open(bytes.data(),rate))throw std::runtime_error("original V2M open failed");
  player->Play();float output[2048]{};
  while(player->IsPlaying()) {
    player->Render(output,1024);
    if(original_v2::sample>std::uint64_t(rate)*1800)throw std::runtime_error("original V2M did not terminate");
  }
  player->Close();return std::move(original_v2::packets);
}
}
''')
