// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/chainline.hpp"
#include "spline_kernel.hpp"
#include <cfenv>
#include <limits>

namespace wz3 {
namespace {
using namespace legacy;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Nearest {int previous=std::fegetround();Nearest(){std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetround(previous);}};
struct KInstanceMem {bool Reset=true;};
template<class T> void sSwap(T& a,T& b) {std::swap(a,b);}
float sFSqrt(float v) {return std::sqrt(double(v));}
sMatrix matrix(const SceneMatrix& m) {
  sMatrix out;out.i.Init(m[0],m[1],m[2],m[3]);out.j.Init(m[4],m[5],m[6],m[7]);out.k.Init(m[8],m[9],m[10],m[11]);out.l.Init(m[12],m[13],m[14],m[15]);return out;
}
#define sVERIFY(x) require(x,"invalid ChainLine kernel state")
#define sCOUNTOF(x) int((x).size())
#define sPLAYER 1
#include "chainline_kernel.inc"
#undef sPLAYER
#undef sCOUNTOF
#undef sVERIFY
}
struct ChainLines::Impl {std::map<std::size_t,CLData> instances;};
ChainLines::ChainLines():impl(std::make_unique<Impl>()) {}
ChainLines::~ChainLines()=default;
SceneRibbon ChainLines::update(std::size_t instance,std::span<const std::uint32_t> p,const SceneMarkers& markers,const FrameClock& clock) {
  const Nearest rounding;
  require(p.size()==21 && p[6]<32 && p[7]<32 && p[16]<=15 && p[20]<32,"invalid ChainLine parameters");
  const auto real=[&](unsigned i) {const auto v=as_float(p[i]);require(std::isfinite(v) && std::abs(v)<=1e6f,"invalid ChainLine numeric parameter");return v;};
  for(unsigned i=0;i<21;++i)if(i!=6 && i!=7 && i!=16 && i!=20)real(i);
  require(clock.current>=0 && clock.delta>=0 && clock.delta<=clock.current,"invalid ChainLine clock");
  const int slices=clock.reset ? 0 : clock.current/10-(clock.current-clock.delta)/10;
  require(slices<=100000,"ChainLine update exceeds 1000 seconds");
  require(impl->instances.contains(instance) || impl->instances.size()<65536,"ChainLine instance limit");
  std::array<sMatrix,32> transforms;
  for(unsigned i=0;i<32;++i) {for(auto v:markers[i])require(std::isfinite(v),"nonfinite ChainLine marker");transforms[i]=matrix(markers[i]);}
  auto& mem=impl->instances[instance];require(mem.TimeCounter<=std::numeric_limits<int>::max()-slices*10,"ChainLine clock overflow");
  simulate(&mem,transforms,slices,{real(0),real(1),real(2),0},{real(3),real(4),real(5),0},p[6],p[7],real(8),real(9),real(10),real(11),real(12),real(13),real(14),real(15),p[16],{real(17),real(18),real(19),0},p[20]);
  mem.Reset=false;SceneRibbon out;out.thickness=real(9);out.ripped=mem.Ripped;
  for(unsigned i=0;i<32;++i) {out.points[i]={mem.Lines[i].Pos.x,mem.Lines[i].Pos.y,mem.Lines[i].Pos.z};for(auto v:out.points[i])require(std::isfinite(v),"nonfinite ChainLine simulation");}
  return out;
}
std::vector<SceneEffectVertex> chainline_vertices(const SceneRibbon& line,const SceneMatrix& model_view) {
  const Nearest rounding;require(line.ripped>=-1 && line.ripped<32 && std::isfinite(line.thickness),"invalid ChainLine ribbon");
  sVector forward{model_view[2],model_view[6],model_view[10],0};require(std::isfinite(forward.Dot3(forward)) && forward.Dot3(forward)>0,"invalid ChainLine viewing direction");forward.Unit3();
  const auto point=[&](unsigned i){const auto& p=line.points[i];return sVector{p[0],p[1],p[2],0};};
  std::array<SceneEffectVertex,64> strip;
  for(unsigned i=0;i<32;++i) {
    sVector direction,side;direction.Sub3(point(i ? i-1 : 0),point(std::min(31u,i+1)));direction.UnitSafe3();
    side.Cross3(direction,forward);side.Scale3(line.thickness/2);
    for(unsigned edge=0;edge<2;++edge) {
      auto v=point(i);v.AddScale3(side,edge ? -1 : 1);
      strip[2*i+edge]={{v.x,v.y,v.z},{float(i),float(edge)},0xff404040};
      for(auto x:strip[2*i+edge].position)require(std::isfinite(x),"nonfinite ChainLine ribbon vertex");
    }
  }
  std::vector<SceneEffectVertex> out;out.reserve(31*4);
  for(unsigned i=0;i<31;++i)for(auto index:(int(i)==line.ripped ? std::array<unsigned,4>{2*i,2*i+1,2*i,2*i+1} : std::array<unsigned,4>{2*i+3,2*i+2,2*i,2*i+1}))out.push_back(strip[index]);
  return out;
}
} // namespace wz3
