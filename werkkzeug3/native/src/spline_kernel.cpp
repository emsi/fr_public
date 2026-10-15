// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "spline_kernel.hpp"
#include "noise.hpp"
#include <cfenv>
namespace wz3::legacy {
namespace {
template<class T> void sSwap(T& a,T& b) {std::swap(a,b);}
float sFSqrt(float v) {return std::sqrt(double(v));}
float sFade(float a,float b,float t) {return a+(b-a)*t;}
int nearest_coordinate(float v) {
  const auto n=std::nearbyint(double(v));
  if(!std::isfinite(n) || n<-2147483648.0 || n>2147483647.0)throw std::runtime_error("spline noise coordinate exceeds int32");
  return int(n);
}
constexpr std::array<sVector,16> G{{
  {1,1,0,0},{-1,1,0,0},{1,-1,0,0},{-1,-1,0,0},
  {1,0,1,0},{-1,0,1,0},{1,0,-1,0},{-1,0,-1,0},
  {0,1,1,0},{0,-1,1,0},{0,1,-1,0},{0,-1,-1,0},
  {1,1,0,0},{-1,1,0,0},{0,-1,1,0},{0,-1,-1,0}}};
}
#define sVERIFY(value) do {if(!(value))throw std::runtime_error("invalid spatial spline kernel state");} while(0)
#include "spline_kernel.inc"
#undef sVERIFY
}
