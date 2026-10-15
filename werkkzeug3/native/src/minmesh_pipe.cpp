// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_kernel.hpp"
#include "spline_kernel.hpp"
#include <cfenv>
namespace wz3::legacy::minimal {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int pipe_integer(float value) {
  require(std::isfinite(value) && double(value)>=-2147483648. && double(value)<2147483648.,"Pipe repeat count overflow");return int(value);
}
double sFACos(double value){return std::acos(value);}
struct PipeSource {
  BlobSpline& spline;BlobPipe& pipe;
  BlobSpline* GetBlobSpline(){return &spline;}
  BlobPipe* GetBlobPipe(){return &pipe;}
};
}
#define sVERIFY(x) require(bool(x),"Pipe kernel invariant: " #x)
#include "pipe_kernel.inc"
#undef sVERIFY
GenMinMesh* pipe_mesh(BlobSpline& spline,BlobPipe& pipe,const GenMinMesh& straight,const GenMinMesh* curved,const GenMinMesh* rings,std::span<const std::uint32_t> p) {
  struct Nearest {int saved=std::fegetround();Nearest(){require(saved!=-1 && std::fesetround(FE_TONEAREST)==0,"cannot set pipe rounding mode");}~Nearest(){std::fesetround(saved);}} rounding;
  require(p.size()==4 && p[0]<=31,"invalid Pipe parameters");
  const auto zoom=as_float(p[1]),ringdist=as_float(p[2]),objdist=as_float(p[3]);
  require(std::isfinite(zoom) && std::isfinite(ringdist) && std::isfinite(objdist),"nonfinite Pipe parameters");
  require(!(p[0]&4) || objdist>0,"Pipe object distance must be positive");
  require(pipe.Count>0 && std::size_t(pipe.Count)==pipe.Keys.size() && spline.Count==pipe.Count*2 && std::size_t(spline.Count)==spline.Keys.size() && spline.Mode==4,"invalid Pipe spline");
  const auto extent=[](const GenMinMesh& input) {
    require(input.Vertices.Count>0,"Pipe source mesh is empty");sAABox box;input.CalcBBox(box);
    require(std::isfinite(box.Max.z-box.Min.z) && box.Max.z>box.Min.z,"Pipe source mesh needs a nonzero Z extent");
  };
  extent(straight);if(curved)extent(*curved);
  PipeSource source{spline,pipe};auto out=std::unique_ptr<GenMinMesh>(MinMesh_Pipe(&source,&straight,curved,rings,p[0],zoom,ringdist,objdist));
  require(bool(out),"Pipe construction failed");out->validate();return out.release();
}
}
