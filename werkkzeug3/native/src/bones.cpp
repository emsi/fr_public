// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_kernel.hpp"
#include "spline_kernel.hpp"
#include "wz3/spline3d.hpp"
#include <cfenv>
namespace wz3::legacy::minimal {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Nearest {
  int saved=std::fegetround();
  Nearest(){require(saved!=-1 && std::fesetround(FE_TONEAREST)==0,"cannot set bone rounding mode");}
  ~Nearest(){std::fesetround(saved);}
};
float chain_inverse_length(const sVector& dir) {
  const auto squared=dir.Dot3(dir);require(std::isfinite(squared) && squared>0,"BoneChain endpoints coincide or overflow");return 1.f/squared;
}
int chain_coordinate(float value) {
  require(std::isfinite(value) && double(value)>=-2147483648. && double(value)<2147483648.,"BoneChain coordinate overflow");return int(value);
}
struct CurveSpline final : GenSpline {
  std::shared_ptr<BlobSpline> curve;
  explicit CurveSpline(std::shared_ptr<BlobSpline> data):curve(std::move(data)){}
  void Eval(float time,float,sMatrix& matrix,float& zoom)override {
    require(std::isfinite(time),"nonfinite bone spline time");curve->Calc(time,matrix,zoom);
  }
  std::size_t bytes()const override{return sizeof(CurveSpline)+sizeof(BlobSpline)+curve->Keys.capacity()*sizeof(BlobSplineKey);}
};
}
#include "bone_kernel.inc"
GenMinMesh* bone_chain(GenMinMesh& mesh,std::span<const std::uint32_t> p) {
  const Nearest rounding;require(p.size()==8 && p[6]>=2 && p[6]<=255 && p[7]<=1,"invalid BoneChain parameters");
  sVector start,end;
  for(unsigned i=0;i<3;++i){start[i]=as_float(p[i]);end[i]=as_float(p[i+3]);require(std::isfinite(start[i]) && std::isfinite(end[i]),"nonfinite BoneChain endpoint");}
  if(p[7]&1)require(mesh.Vertices.Count>0,"automatic BoneChain needs vertices");
  return MinMesh_BoneChain(&mesh,start,end,p[6],p[7]);
}
GenMinMesh* bone_train(GenMinMesh& mesh,std::shared_ptr<GenSpline> spline,float delta,unsigned mode,float offset) {
  const Nearest rounding;require(std::isfinite(delta) && std::isfinite(offset) && mode<=7,"invalid BoneTrain parameters");
  if(spline && mesh.Animation && mesh.Animation->Matrices.Count==1)require((mode&3)>=2,"BoneTrain needs two bones for shift/scale mode");
  return MinMesh_BoneTrain(&mesh,std::move(spline),delta,mode,offset);
}
std::shared_ptr<GenSpline> bone_spline(const Project& p,const Graph& g,std::size_t id) {
  SpatialSplines library(p,g);auto env=frame_environment(0);
  return std::make_shared<CurveSpline>(library.static_curve(id,env));
}
}
