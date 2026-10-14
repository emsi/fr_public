// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// sInitPerlin from _types.cpp and GenMesh::Perlin from genmesh.cpp.
#include "mesh_kernel.hpp"
#include <cfenv>

namespace wz3::legacy {
namespace {
class DownwardRounding {
  std::fenv_t previous;
public:
  DownwardRounding() {
    if(std::fegetenv(&previous)!=0)throw std::runtime_error("cannot save mesh floating-point environment");
    if(std::fesetround(FE_DOWNWARD)!=0) {
      std::fesetenv(&previous);throw std::runtime_error("cannot set mesh downward rounding");
    }
  }
  ~DownwardRounding() {std::fesetenv(&previous);}
  DownwardRounding(const DownwardRounding&)=delete;
};
int floor_coordinate(float v) {
  const auto result=std::floor(double(v));
  if(!std::isfinite(result) || result<-2147483648.0 || result>2147483647.0)
    throw std::runtime_error("mesh noise coordinate exceeds int32 range");
  return static_cast<int>(result);
}
struct NoiseData {
  std::array<std::uint8_t,512> permutation{};
  std::array<sVector,16> gradient{{
    {1,1,0,0},{-1,1,0,0},{1,-1,0,0},{-1,-1,0,0},
    {1,0,1,0},{-1,0,1,0},{1,0,-1,0},{-1,0,-1,0},
    {0,1,1,0},{0,-1,1,0},{0,1,-1,0},{0,-1,-1,0},
    {1,1,0,0},{-1,1,0,0},{0,-1,1,0},{0,-1,-1,0}}};
  NoiseData() {
    // The archive seeds its LCG with one, warms it four times, then sorts 256
    // random keys using pairwise swaps. A standard shuffle changes the artwork.
    constexpr std::uint32_t mixed=1+17+121+121/17;
    auto state=mixed;
    const auto next=[&]() {
      const auto first=state*0x343fdu+0x269ec3u;state=first*0x343fdu+0x269ec3u;
      return ((state>>10)&0xffffu)|((first<<6)&0xffff0000u);
    };
    for(unsigned i=0;i<4;++i) {if(i)state^=mixed;next();}
    std::array<std::uint32_t,256> keys{};
    for(unsigned i=0;i<256;++i) {keys[i]=next()%65536;permutation[i]=i;}
    for(unsigned i=0;i<255;++i)for(unsigned j=i+1;j<256;++j)if(keys[i]>keys[j]) {
      std::swap(keys[i],keys[j]);std::swap(permutation[i],permutation[j]);
    }
    std::copy_n(permutation.begin(),256,permutation.begin()+256);
  }
};
const NoiseData& noise_data() {static const NoiseData data;return data;}
}
#include "mesh_noise.inc"
} // namespace wz3::legacy
