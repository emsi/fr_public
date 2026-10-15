// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_kernel.hpp"
#include "particle_random.hpp"
#include <cfenv>
namespace wz3::legacy::minimal {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
thread_local ParticleRandom random;
void sSetRndSeed(unsigned seed){random.seed(seed);}
float sFGetRnd(){return random.real();}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "minmesh_multiply.inc"
#pragma GCC diagnostic pop
}
GenMinMesh* multiply_mesh(const GenMinMesh& input,std::span<const std::uint32_t> p) {
  struct Nearest {
    std::fenv_t saved;
    Nearest(){require(std::fegetenv(&saved)==0 && std::fesetround(FE_TONEAREST)==0,"cannot set Multiply rounding");}
    ~Nearest(){std::fesetenv(&saved);}
  } rounding;
  require(p.size()==17 && p[9]<=255 && p[10]<=3,"invalid MINMESH Multiply parameters");
  const auto real=[&](unsigned i){const auto v=as_float(p[i]);require(std::isfinite(v) && std::abs(v)<=1000000,"invalid MINMESH Multiply value");return v;};
  sFSRT srt{};for(unsigned i=0;i<9;++i)srt.v[i]=real(i);
  const sVector rotation{real(13),real(14),real(15),0};
  auto out=std::unique_ptr<GenMinMesh>(MinMesh_Multiply(nullptr,&input,srt,p[9],p[10],real(11),real(12),rotation,real(16)));
  out->materials=input.materials;out->validate();return out.release();
}
}
