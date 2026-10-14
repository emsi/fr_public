// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// GenMesh::Displace from genmesh.cpp, using the native MMX-compatible sampler.
#include "mesh_kernel.hpp"
#include "wz3/texture.hpp"
#include <bit>

namespace wz3::legacy {
namespace {
std::int32_t fixed_coordinate(float value) {
  // The original Win32 __ftol2 converts through int64 and returns its low
  // word to the int32 sampler (player_intro/minirtl.asm). Theta's band exceeds
  // int32 here. Preserve that wrap without an undefined float-to-int32 cast.
  if(!std::isfinite(value) || double(value)<-0x1p63 || double(value)>=0x1p63)
    throw std::runtime_error("mesh displacement coordinate exceeds int64 range");
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(value)));
}
}
void GenMesh::Displace(const Texture& bitmap,float amplx,float amply,float amplz) {
  const float x_scale=bitmap.width*65536.0f,y_scale=bitmap.height*65536.0f;
  for(int i=0;i<Vert.Count;++i)if(Vert[i].Select) {
    int v=i,count=0;
    sVector displacement;
    displacement.Init(0,0,0,0);
    do {
      if(v<0 || v>=Vert.Count || ++count>Vert.Count)throw std::runtime_error("invalid displacement vertex cycle");
      const auto& uv=buffer[v*5+4];const auto& n=buffer[v*5+1];
      const auto sample=sample_bitmap(bitmap,fixed_coordinate(uv.x*x_scale),fixed_coordinate(uv.y*y_scale),0);
      // The archive stores BGRA; the portable texture API exposes RGBA.
      const float height=(sample[2]-16384.0f)*(1.0f/32767.0f);
      displacement.x+=n.x*height*amplx;displacement.y+=n.y*height*amply;displacement.z+=n.z*height*amplz;
      Vert[v].Select=0;v=Vert[v].Next;
    }while(v!=i);
    displacement.Scale3(1.0f/count);
    do {VertPos(v).Add3(displacement);v=Vert[v].Next;}while(v!=i);
  }
}
} // namespace wz3::legacy
