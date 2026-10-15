// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "spline_reference.hpp"
#include "spline_kernel.hpp"
namespace reference {
namespace {
using namespace wz3::legacy;
constexpr int KV_TIME=0;
constexpr float sPIF=sPI;
struct KEvent {int Start{},End{};KEvent* NextOp=nullptr;};
struct KOp {KEvent* FirstEvent=nullptr;};
struct Environment {std::array<sVector,32> Var;int BeatTime=0;};
struct Overlay {Environment* KEnv=nullptr;};
thread_local Overlay* GenOverlayManager=nullptr;
struct GenSpline {
  sMatrix matrix;float zoom=1;
  void Eval(float,float,sMatrix& m,float& z) {m=matrix;z=zoom;}
};
struct GenSplineShaker {
  GenSpline *Parent=nullptr,*AmpSpline=nullptr;KOp* OpLink=nullptr;int Mode=0;
  sVector TranslateAmp,RotateAmp,TranslateFreq,RotateFreq,Center;
  void Eval(float,float,sMatrix&,float&);
};
#include "spline_oracle.inc"
}
wz3::SplinePose shake(std::span<const std::uint32_t> p,const wz3::SplinePose& parent,float amplitude,float global_time,
                      std::span<const std::array<std::int32_t,2>> events,std::int32_t beat) {
  GenSpline fixed,amp;const auto& w=parent.world;
  fixed.matrix.i.Init(w[0],w[1],w[2],w[3]);fixed.matrix.j.Init(w[4],w[5],w[6],w[7]);
  fixed.matrix.k.Init(w[8],w[9],w[10],w[11]);fixed.matrix.l.Init(w[12],w[13],w[14],w[15]);fixed.zoom=parent.zoom;amp.zoom=amplitude;
  std::vector<KEvent> active(events.size());
  for(std::size_t i=0;i<events.size();++i) {active[i].Start=events[i][0];active[i].End=events[i][1];if(i+1<events.size())active[i].NextOp=&active[i+1];}
  KOp op;if(!active.empty())op.FirstEvent=&active[0];
  Environment env;env.Var[0].x=global_time;env.BeatTime=beat;Overlay overlay{&env};GenOverlayManager=&overlay;
  GenSplineShaker s;s.Parent=&fixed;s.AmpSpline=&amp;s.OpLink=&op;s.Mode=p[0];
  const auto v=[&](unsigned i){sVector result;result.Init(wz3::as_float(p[i]),wz3::as_float(p[i+1]),wz3::as_float(p[i+2]));return result;};
  s.TranslateAmp=v(1);s.RotateAmp=v(4);s.TranslateFreq=v(7);s.RotateFreq=v(10);s.Center=v(14);
  sMatrix m;float zoom;s.Eval(global_time,0,m,zoom);GenOverlayManager=nullptr;
  return {{m.i.x,m.i.y,m.i.z,0,m.j.x,m.j.y,m.j.z,0,m.k.x,m.k.y,m.k.z,0,m.l.x,m.l.y,m.l.z,1},zoom};
}
}
