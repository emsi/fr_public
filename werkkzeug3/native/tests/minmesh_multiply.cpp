// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "minmesh_reference.hpp"
#include <cfenv>
#include <iostream>
#include <numbers>
namespace {
using namespace wz3;namespace mm=legacy::minimal;
using Point=std::array<double,3>;
using SRT=std::array<float,9>;
Point transform(Point v,const SRT& s) {
  for(unsigned c=0;c<3;++c)v[c]*=s[c];
  // Independent scale, X/Y/Z rotations and translation in double precision.
  for(unsigned axis=0;axis<3;++axis) {
    const unsigned a=(axis+1)%3,b=(axis+2)%3;
    const auto angle=s[axis+3]*2*std::numbers::pi,co=std::cos(angle),si=std::sin(angle);
    const auto x=v[a],y=v[b];v[a]=x*co-y*si;v[b]=x*si+y*co;
  }
  for(unsigned c=0;c<3;++c)v[c]+=s[c+6];return v;
}
struct Random {
  std::uint32_t state;
  explicit Random(unsigned seed) {
    const auto mix=seed*139u+seed*121u/17;state=mix;
    for(unsigned i=0;i<4;++i){if(i)state^=mix;next();}
  }
  std::uint32_t next(){const auto a=state*214013u+2531011u;state=a*214013u+2531011u;return ((state>>10)&65535u)|((a<<6)&0xffff0000u);}
  float real(){return float(next()&0x3fffffffu)/1073741824.f;}
};
void compare(const mm::GenMinMesh& source,const mm::GenMinMesh& out,std::span<const std::uint32_t> p) {
  const auto count=p[9],mode=p[10];check(out.Vertices.Count==int(count)*source.Vertices.Count && out.Faces.Count==int(count)*source.Faces.Count,"Multiply copy count");
  SRT step{},local{1,1,1},random{1,1,1};for(unsigned i=0;i<9;++i)step[i]=as_float(p[i]);for(unsigned c=0;c<3;++c)local[c+3]=as_float(p[13+c]);Random rng(count);
  for(unsigned copy=0;copy<count;++copy) {
    for(int i=0;i<source.Vertices.Count;++i) {
      const auto& v=source.Vertices[i];const auto& actual=out.Vertices[copy*source.Vertices.Count+i];Point point{v.Pos.x,v.Pos.y,v.Pos.z};
      for(unsigned j=0;j<copy;++j)point=transform(point,local);
      if(mode&2)point=transform(point,random);else for(unsigned j=0;j<copy;++j)point=transform(point,step);
      const float xyz[]={actual.Pos.x,actual.Pos.y,actual.Pos.z};for(unsigned c=0;c<3;++c)check(std::abs(xyz[c]-point[c])<.00005,"Multiply position differs from independent SRT composition");
      check(actual.Color==v.Color && actual.Select==v.Select && actual.MergeTag==v.MergeTag && actual.BoneCount==v.BoneCount,"Multiply vertex attributes");
      for(unsigned c=0;c<4;++c)check(actual.Weights[c]==v.Weights[c] && actual.Matrix[c]==v.Matrix[c],"Multiply bone attributes");
      for(unsigned uv=0;uv<2;++uv)for(unsigned c=0;c<2;++c)near(actual.UV[uv][c],v.UV[uv][c]+((!uv && (mode&1)) ? copy*as_float(p[11+c]) : 0),"Multiply UV shift");
    }
    for(int i=0;i<source.Faces.Count;++i) {
      const auto& face=source.Faces[i];const auto& actual=out.Faces[copy*source.Faces.Count+i];
      check(actual.Count==face.Count && actual.Select==face.Select && actual.Flags==face.Flags,"Multiply face attributes");
      auto a=out.Clusters[actual.Cluster],b=source.Clusters[face.Cluster];
      check((a.Mtrl ? a.Mtrl->binding : std::nullopt)==(b.Mtrl ? b.Mtrl->binding : std::nullopt),"Multiply material binding changed");
      a.Mtrl=b.Mtrl=nullptr;check(a.Equals(b),"Multiply material cluster changed");
      for(unsigned c=0;c<face.Count;++c)check(actual.Vertices[c]==int(copy)*source.Vertices.Count+face.Vertices[c],"Multiply corner index");
    }
    if(mode&2){for(unsigned c=0;c<3;++c)random[c]=(step[c]-1)*rng.real()+1;for(unsigned c=3;c<9;++c)random[c]=step[c]*(rng.real()-.5f)*2;}
  }
}
std::array<std::uint32_t,17> parameters(unsigned count,unsigned mode) {
  return {f(1),f(1),f(1),0,0,0,f(2),0,0,count,mode,f(.125f),f(-.25f),0,0,0,f(.7f)};
}
void controlled() {
  legacy::sFSRT unit{};unit.v[0]=unit.v[1]=unit.v[2]=1;
  const auto source=std::unique_ptr<mm::GenMinMesh>(mm::MinMesh_Cube(1,1,1,0,unit));
  for(int i=0;i<source->Vertices.Count;++i){auto& v=source->Vertices[i];v.Color=0xff102030+i;v.Select=i&1;v.MergeTag=i%3;v.UV[1][0]=i*.1f;v.UV[1][1]=-.2f;v.BoneCount=1;v.Weights[0]=1;v.Matrix[0]=3;}
  source->materials=std::make_shared<mm::MaterialArena>();auto& material=(*source->materials)[42];material.binding=42;
  source->Clusters[1].Mtrl=&material;source->Clusters[1].RenderPass=7;
  for(int i=0;i<source->Faces.Count;++i){source->Faces[i].Select=i&1;source->Faces[i].Flags=i&1;}
  unsigned cases=0;
  for(unsigned mode=0;mode<4;++mode)for(unsigned count:{0u,1u,2u,8u,254u}) {
    const auto p=parameters(count,mode);const auto out=std::unique_ptr<mm::GenMinMesh>(mm::multiply_mesh(*source,p));compare(*source,*out,p);++cases;
  }
  for(unsigned mode=0;mode<4;++mode)for(unsigned variation=0;variation<8;++variation) {
    auto p=parameters(8,mode);p[0]=f(variation&1 ? -.9f : 1.05f);p[1]=f(.8f);p[2]=f(1.1f);
    p[3]=f(.02f*variation);p[4]=f(.13f);p[5]=f(-.09f);p[7]=f(-.5f);p[8]=f(.2f);
    p[13]=f(.01f);p[14]=f(.03f*variation);p[15]=f(-.08f);
    const auto out=std::unique_ptr<mm::GenMinMesh>(mm::multiply_mesh(*source,p));compare(*source,*out,p);++cases;
  }
  auto p=parameters(2,0);std::fesetround(FE_DOWNWARD);std::feraiseexcept(FE_DIVBYZERO);const auto out=std::unique_ptr<mm::GenMinMesh>(mm::multiply_mesh(*source,p));
  check(std::fegetround()==FE_DOWNWARD && (std::fetestexcept(FE_DIVBYZERO)!=0),"Multiply leaked floating-point environment");
  bool caught=false;p[0]=f(INFINITY);try{std::unique_ptr<mm::GenMinMesh> bad(mm::multiply_mesh(*source,p));}catch(const std::runtime_error&){caught=true;}
  check(caught && std::fegetround()==FE_DOWNWARD,"Multiply overflow/rounding restoration");std::fesetround(FE_TONEAREST);std::feclearexcept(FE_ALL_EXCEPT);
  // The original starts with two clusters and appends before merging. Copy
  // 255 would wrap the byte-sized face cluster, so the bounded host rejects it.
  caught=false;p=parameters(255,0);try{std::unique_ptr<mm::GenMinMesh> bad(mm::multiply_mesh(*source,p));}catch(const std::runtime_error&){caught=true;}
  check(caught,"Multiply accepted overflowing cluster indices");
  check(source->Vertices.Count==24 && source->Faces.Count==6 && source->Vertices[0].Pos.x==-.5f,"Multiply mutated borrowed source");
  std::cout<<cases<<" Multiply count/UV/random/local-rotation combinations match independent SRT arithmetic\n";
}
void debris(const char* project,const char* font) {
  const auto p=read_project(project);const auto g=connect(p);TextureOptions options;options.font_file=font;unsigned count=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0x12f) {
    const auto input=evaluate_mesh(p,g,g.nodes[id].inputs[0],options),output=evaluate_mesh(p,g,id,options);
    compare(*input->min_kernel,*output->min_kernel,p.operators[id].parameters);
    std::vector<std::uint8_t> mask;for(int i=0;i<output->min_kernel->Faces.Count;++i){const auto& face=output->min_kernel->Faces[i];mask.push_back(face.Cluster && face.Count>=3 && !(face.Flags&1));}
    check_minmesh_shadow_reference(*output,mask);++count;
  }
  check(count==7,"missing saved Multiply recipes");std::cout<<count<<" complete saved Multiply meshes match repeated transforms and archived shadow topology\n";
}
}
int main(int argc,char** argv){try{controlled();if(argc>2)debris(argv[1],argv[2]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
