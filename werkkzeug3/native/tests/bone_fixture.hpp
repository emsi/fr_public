// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "frame_fixture.hpp"
#include "bone_reference.hpp"
struct BoneFixture:FrameFixture {
  std::size_t curve() {
    const auto id=add(0x18);std::vector<std::uint32_t> blob(16);blob[0]=4;blob[1]=3;blob[3]=3;
    for(const auto& key:std::array<std::array<float,4>,4>{{{0,0,0,0},{.3f,1,0,2},{.7f,-1,1,6},{1,0,1,8}}})
      blob.insert(blob.end(),{0,f(key[0]),f(key[1]),f(key[2]),f(key[3]),0,0,0,f(1)});
    p.operators[id].blob_offset=p.bytes.size();p.operators[id].blob_size=blob.size()*4;
    for(auto word:blob)for(unsigned i=0;i<4;++i)p.bytes.push_back(std::uint8_t(word>>(i*8)));
    return id;
  }
  std::size_t train(unsigned mode=0) {
    const auto cube=add(0x102,{1,1,16,0,f(.7f),f(.7f),f(8),0,0,0,0,0,f(4)});
    const auto chain=add(0x128,{0,0,0,0,0,f(8),9,0},{cube});
    return add(0x129,{f(.8f),mode,f(-.2f)},{chain,curve()});
  }
};
