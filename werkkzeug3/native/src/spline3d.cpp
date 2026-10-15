// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/spline3d.hpp"
#include "spline_kernel.hpp"
#include <bit>
#include <cfenv>
namespace wz3 {
namespace {
using legacy::BlobSpline;using legacy::sMatrix;using legacy::sVector;using legacy::sQuaternion;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Nearest {
  int saved=std::fegetround();
  Nearest() {require(saved!=-1 && std::fesetround(FE_TONEAREST)==0,"cannot set spline rounding mode");}
  ~Nearest() {std::fesetround(saved);}
};
struct Visit {
  std::vector<bool>& seen;std::size_t id;
  Visit(std::vector<bool>& s,std::size_t i):seen(s),id(i) {require(!seen[id],"cycle in spatial splines");seen[id]=true;}
  ~Visit() {seen[id]=false;}
};
float real(std::uint32_t word) {
  const auto value=as_float(word);require(std::isfinite(value) && std::abs(value)<=1e15f,"invalid spatial spline number");return value;
}
std::uint32_t word(std::span<const std::uint8_t> data,std::size_t offset) {
  require(offset<=data.size() && data.size()-offset>=4,"truncated spatial spline blob");
  return std::uint32_t(data[offset]) | std::uint32_t(data[offset+1])<<8 | std::uint32_t(data[offset+2])<<16 | std::uint32_t(data[offset+3])<<24;
}
std::span<const std::uint8_t> blob(const Project& p,const Operator& op) {
  require(op.blob_offset<=p.bytes.size() && op.blob_size<=p.bytes.size()-op.blob_offset,"invalid spatial spline blob range");
  return std::span(p.bytes).subspan(op.blob_offset,op.blob_size);
}
std::shared_ptr<BlobSpline> curve(std::span<const std::uint8_t> bytes) {
  require(bytes.size()>=64,"truncated spatial spline header");auto out=std::make_shared<BlobSpline>();
  const auto count=word(bytes,0),version=word(bytes,4),mode=word(bytes,12);
  require(count>=2 && count<=65536 && bytes.size()-64>=std::size_t(count)*36,"invalid spatial spline key count");
  require(version>=1 && version<=3 && mode<=5,"unsupported spatial spline version or mode");
  out->Count=count;out->Mode=mode;
  out->Target.Init(real(word(bytes,16)),real(word(bytes,20)),real(word(bytes,24)),real(word(bytes,28)));
  if(version>=3) {out->Continuity=real(word(bytes,32));out->Tension=real(word(bytes,36));out->Uniform=word(bytes,40)!=0;}
  out->Keys.resize(count);
  for(unsigned i=0;i<count;++i) {
    const auto base=64+36*i;auto& k=out->Keys[i];k.Time=real(word(bytes,base+4));
    k.px=real(word(bytes,base+8));k.py=real(word(bytes,base+12));k.pz=real(word(bytes,base+16));
    k.rx=real(word(bytes,base+20));k.ry=real(word(bytes,base+24));k.rz=real(word(bytes,base+28));k.Zoom=real(word(bytes,base+32));
    if(i)require(k.Time>=out->Keys[i-1].Time,"unordered spatial spline keys");
  }
  // Saved blobs can retain unused capacity beyond Count; the original copies
  // only live keys. Version 1/2 resets tension, continuity and uniform timing.
  return out;
}
std::shared_ptr<BlobSpline> pipe(std::span<const std::uint8_t> bytes,std::shared_ptr<legacy::BlobPipe>& original) {
  require(bytes.size()>=64,"truncated pipe spline header");const auto count=word(bytes,0);
  require(word(bytes,4)==1 && count && count<=32768 && bytes.size()-64>=std::size_t(count)*32,"invalid pipe spline version or key count");
  auto out=std::make_shared<BlobSpline>();out->Mode=4;out->Count=2*count;out->Keys.resize(out->Count);out->Tension=real(word(bytes,12));
  original=std::make_shared<legacy::BlobPipe>();original->Count=count;original->Keys.resize(count);
  sMatrix mat;mat.InitEuler(0,0,real(word(bytes,28))*legacy::sPI2F);float old_radius=0;
  sVector v;v.Init(real(word(bytes,16)),real(word(bytes,20)),real(word(bytes,24)));
  for(unsigned i=0;i<count;++i) {
    const auto base=64+32*i;sVector p=v,d,axis;v.Init(real(word(bytes,base)),real(word(bytes,base+4)),real(word(bytes,base+8)));
    d.Sub3(v,p);d.UnitSafe3();const float cosine=std::clamp(mat.k.Dot3(d),-1.f,1.f);
    const float angle=std::acos(double(cosine));axis.Cross3(mat.k,d);sMatrix rotation,next;rotation.InitRot(axis,angle);
    next.Mul3(mat,rotation);mat=next;sQuaternion quat;quat.FromMatrix(mat);
    p.AddScale3(d,old_radius);
    auto& first=out->Keys[2*i];first.Time=i+0.f;first.px=p.x;first.py=p.y;first.pz=p.z;first.rx=quat.x;first.ry=quat.y;first.rz=quat.z;first.Zoom=quat.w;
    old_radius=real(word(bytes,base+12));p=v;p.AddScale3(d,-old_radius);
    original->Keys[i]={v.x,v.y,v.z,old_radius};
    auto& second=out->Keys[2*i+1];second.Time=i+.5f;second.px=p.x;second.py=p.y;second.pz=p.z;second.rx=quat.x;second.ry=quat.y;second.rz=quat.z;second.Zoom=quat.w;
  }
  out->Normalize();return out;
}
SplinePose pose(const sMatrix& m,float zoom) {
  SplinePose result{{m.i.x,m.i.y,m.i.z,0,m.j.x,m.j.y,m.j.z,0,m.k.x,m.k.y,m.k.z,0,m.l.x,m.l.y,m.l.z,1},zoom};
  for(auto v:result.world)require(std::isfinite(v),"spatial spline produced a nonfinite matrix");
  require(std::isfinite(zoom),"spatial spline produced a nonfinite zoom");return result;
}
}
struct SpatialSplines::Impl {
  struct Node {
    std::size_t id{};
    std::shared_ptr<BlobSpline> data;
    std::shared_ptr<legacy::BlobPipe> pipe;
    std::shared_ptr<Node> parent,amplitude;
    unsigned mode{};
    sVector translate_amp,rotate_amp,translate_freq,rotate_freq,center;
  };
  const Project& project;const Graph& graph;SplineLibrary curves;
  std::vector<std::shared_ptr<Node>> cache;std::vector<bool> visiting;std::size_t stored_keys=0;
  Impl(const Project& p,const Graph& g):project(p),graph(g),curves(p),cache(p.operators.size()),visiting(cache.size()) {require(g.nodes.size()==cache.size(),"spatial spline graph size mismatch");}
  std::shared_ptr<Node> prepare(std::size_t id,AnimationEnvironment& env,unsigned depth=0) {
    require(id<cache.size() && graph.nodes[id].active,"invalid spatial spline operator");
    if(cache[id])return cache[id];
    require(depth<512,"spatial spline graph depth limit");Visit visit(visiting,id);
    const auto& op=project.operators[id];const auto& node=graph.nodes[id];
    const AnimationScope scope(compile_animation(op,curves),curves,op.parameters,env);const auto& p=scope.parameters;
    std::shared_ptr<Node> result;
    if(op.command>=1 && op.command<=3) {
      if(op.command==1) {require(node.links.size()==1 && node.links[0],"unresolved spatial spline Load");result=prepare(*node.links[0],env,depth+1);}
      else {require(node.inputs.size()==1,"invalid spatial spline alias");result=prepare(node.inputs[0],env,depth+1);}
    }else {
      result=std::make_shared<Node>();result->id=id;
      if(op.command==0x18 || op.command==0x1a) {
        require(p.empty() && node.inputs.empty(),"invalid spatial spline blob operator");
        result->data=op.command==0x18 ? curve(blob(project,op)) : pipe(blob(project,op),result->pipe);
      }else if(op.command==0x1c) {
        require(p.size()==4 && node.inputs.size()==1 && !(p[3]&~7u),"invalid SplineScale layout");
        const auto source=prepare(node.inputs[0],env,depth+1);require(bool(source->data),"SplineScale requires a plain or pipe spline");
        result->data=std::make_shared<BlobSpline>(*source->data);const float x=real(p[0]),y=real(p[1]),z=real(p[2]);
        for(auto& k:result->data->Keys) {k.px*=x;k.py*=y;k.pz*=z;if(p[3]&1)k.rx=0;if(p[3]&2)k.ry=0;if(p[3]&4)k.rz=0;}
      }else if(op.command==0x19) {
        require(p.size()==17 && node.inputs.size()>=1 && node.inputs.size()<=2 && !(p[0]&~0x33u),"invalid ShakeEvent layout");
        result->parent=prepare(node.inputs[0],env,depth+1);if(node.inputs.size()==2)result->amplitude=prepare(node.inputs[1],env,depth+1);
        result->mode=p[0];result->translate_amp.Init(real(p[1]),real(p[2]),real(p[3]));result->rotate_amp.Init(real(p[4]),real(p[5]),real(p[6]));
        result->translate_freq.Init(real(p[7]),real(p[8]),real(p[9]));result->rotate_freq.Init(real(p[10]),real(p[11]),real(p[12]));
        result->center.Init(real(p[14]),real(p[15]),real(p[16]));
      }else throw std::runtime_error("spatial spline operator "+std::to_string(id)+" ("+std::string(operator_info(op.command).name)+") is not ported");
      if(result->data) {require(result->data->Keys.size()<=(1u<<20)-stored_keys,"spatial spline key budget exceeded");stored_keys+=result->data->Keys.size();}
    }
    cache[id]=result;return result;
  }
  void evaluate(const Node& node,float time,float phase,const AnimationEnvironment& env,const EventTimeline* timeline,sMatrix& mat,float& zoom,unsigned depth=0) const {
    require(depth<512,"spatial spline evaluation depth limit");
    if(node.data) {node.data->Calc(time,mat,zoom);return;}
    float amp=1;if(node.amplitude)evaluate(*node.amplitude,time,phase,env,timeline,mat,amp,depth+1);
    evaluate(*node.parent,time,phase,env,timeline,mat,zoom,depth+1);
    const auto shake=[&](float t) {
      if(!(t>0 && t<1))return;
      float ramp=0;switch(node.mode&15) {
      case 0:ramp=1-t;break;case 1:ramp=t;t=1-t;break;case 2:ramp=legacy::sFSin(t*legacy::sPI);break;case 3:ramp=1;break;
      }ramp*=amp;
      sVector tr,ro;
      if(node.mode&32) {
        tr=node.translate_freq;tr.Scale3(t);legacy::sPerlin3D(tr,tr);
        ro=node.rotate_freq;ro.Scale3(t);legacy::sPerlin3D(ro,ro);
      }else for(unsigned i=0;i<3;++i) {
        tr[i]=legacy::sFSin(t*node.translate_freq[i]*legacy::sPI2F)*node.translate_amp[i]*ramp;
        ro[i]=legacy::sFSin(t*node.rotate_freq[i]*legacy::sPI2F)*node.rotate_amp[i]*ramp;
      }
      // Preserve the archived double amplitude/ramp for sine shake and its
      // use of TranslateAmp.y for the final Z multiplier.
      tr.x=tr.x*node.translate_amp.x*ramp;tr.y=tr.y*node.translate_amp.y*ramp;tr.z=tr.z*node.translate_amp.y*ramp;
      for(unsigned i=0;i<3;++i)ro[i]=ro[i]*node.rotate_amp[i]*ramp;
      sMatrix transform,previous;transform.InitEuler(ro.x,ro.y,ro.z);transform.l.Init(tr.x,tr.y,tr.z,1);
      if(node.mode&16) {mat.l.Sub3(node.center);previous=mat;mat.MulA(previous,transform);mat.l.Add3(node.center);}
      else {previous=mat;mat.MulA(transform,previous);}
    };
    const auto events=timeline ? timeline->active(node.id) : std::span<const std::size_t>{};
    if(events.empty())shake(env.variables[0][0]);
    else for(const auto id:events) {
      const auto& event=timeline->event(id);const auto start=std::bit_cast<std::int32_t>(event.fields[1]),end=std::bit_cast<std::int32_t>(event.fields[2]);
      // The original divides by zero for instantaneous events; its resulting
      // NaN/Inf fails the open-interval test and contributes no shake.
      if(end==start)continue;
      shake(float(std::int64_t(end)-timeline->beat())/float(std::int64_t(end)-start));
    }
  }
};
SpatialSplines::SpatialSplines(const Project& p,const Graph& g):impl(std::make_unique<Impl>(p,g)) {}
SpatialSplines::~SpatialSplines()=default;
void SpatialSplines::precalculate(std::size_t id,AnimationEnvironment& env) {const Nearest rounding;impl->prepare(id,env);}
std::shared_ptr<legacy::BlobSpline> SpatialSplines::static_curve(std::size_t id,AnimationEnvironment& env) {
  const Nearest rounding;const auto node=impl->prepare(id,env);
  require(bool(node->data),"bone spline needs a static plain, pipe or scaled curve");return node->data;
}
std::pair<std::shared_ptr<legacy::BlobSpline>,std::shared_ptr<legacy::BlobPipe>> SpatialSplines::pipe_curve(std::size_t id,AnimationEnvironment& env) {
  const Nearest rounding;const auto node=impl->prepare(id,env);
  require(node->data && node->data->Mode==4 && node->pipe,"Pipe mesh requires an original pipe spline");
  return {node->data,node->pipe};
}
SplinePose SpatialSplines::evaluate(std::size_t id,float time,float phase,const AnimationEnvironment& env,const EventTimeline* timeline) {
  const Nearest rounding;require(std::isfinite(time) && std::isfinite(phase),"invalid spatial spline evaluation time");
  auto initial=frame_environment(0);const auto node=impl->prepare(id,initial);sMatrix mat;float zoom=1;
  impl->evaluate(*node,time,phase,env,timeline,mat,zoom);return pose(mat,zoom);
}
}
