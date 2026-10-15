// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/water.hpp"
#include "spline_kernel.hpp"
#include <bit>
#include <cfenv>
namespace wz3 {
namespace {
using legacy::sInt;using legacy::sF32;using legacy::sVector;using legacy::BlobSpline;using legacy::sSetMem;
using sU16=std::uint16_t;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
// The archive's sFPow takes and returns double; the intermediate divisions
// round only when assigned to the Water handler's float variables.
double sFPow(double a,double b){return std::pow(a,b);}
void sQuad(sU16*& output,int a,int b,int c,int d){for(auto index:{a,b,c,a,c,d})*output++=sU16(index);}
struct Nearest {int saved=std::fegetround();Nearest(){require(saved!=-1 && std::fesetround(FE_TONEAREST)==0,"cannot set Water rounding");}~Nearest(){std::fesetround(saved);}};
#include "water_kernel.inc"
}
WaterGeometry water_geometry(const WaterEffect& effect) {
  const Nearest rounding;
  const auto& p=effect.parameters;
  require(effect.spline && effect.spline->Count>=0 && std::size_t(effect.spline->Count)==effect.spline->Keys.size() && effect.spline->Count<=65536,"invalid Water spline");
  require(p[5]>=1 && p[5]<=8,"Water grid must be 1..8");
  const auto valid=[](float v){return std::isfinite(v) && std::abs(v)<=1e6f;};
  for(unsigned i=0;i<5;++i)require(valid(as_float(p[i])),"invalid Water parameter");
  require(as_float(p[0])!=0 && valid(effect.time),"invalid Water scale/time");
  for(const auto& key:effect.spline->Keys) {
    require(valid(key.Time) && valid(key.px) && valid(key.pz) && valid(key.rx) && valid(key.ry) && valid(key.rz) && valid(key.Zoom) && key.rz>0,"invalid Water wave key");
  }
  WaterGeometry out;calculate_water(effect.spline.get(),effect.time,as_float(p[0]),as_float(p[1]),as_float(p[2]),as_float(p[3]),as_float(p[4]),p[5],out);
  for(std::size_t i=0;i<out.vertices.size();++i)if(i%16!=6 && i%16!=7)require(std::isfinite(out.vertices[i]),"Water produced nonfinite geometry");
  return out;
}
}
