// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "minmesh_reference.hpp"
#include <cfenv>
#include <iostream>
#include <limits>
namespace {
using namespace wz3;namespace mm=legacy::minimal;
legacy::sFSRT transform(std::span<const std::uint32_t> p) {legacy::sFSRT out{};for(unsigned i=0;i<9;++i)out.v[i]=as_float(p[i+1]);return out;}
void compare(const mm::GenMinMesh& source,const mm::GenMinMesh& actual,std::span<const std::uint32_t> p) {
  legacy::GenMesh expected;expected.Init(legacy::sGMF_DEFAULT,source.Vertices.Count);
  for(int i=0;i<source.Vertices.Count;++i){const auto id=expected.AddNewVert();const auto& v=source.Vertices[i];expected.VertPos(id).Init(v.Pos.x,v.Pos.y,v.Pos.z);
    expected.Vert[id].Select=p[0]==0 || (p[0]==1 && v.Select) || (p[0]==2 && !v.Select);}
  legacy::sMatrix matrix;const auto srt=transform(p);matrix.InitSRT(srt.v);expected.Perlin(matrix,{as_float(p[10]),as_float(p[11]),as_float(p[12]),0});
  check(actual.Vertices.Count==source.Vertices.Count && actual.Faces.Count==source.Faces.Count,"Perlin changed topology");
  for(int i=0;i<source.Vertices.Count;++i){const auto& a=actual.Vertices[i];const auto& b=expected.VertPos(i);const auto& v=source.Vertices[i];
    check(a.Pos.x==b.x && a.Pos.y==b.y && a.Pos.z==b.z,"MINMESH Perlin differs from archived GenMesh Perlin");
    check(a.Select==v.Select && a.Color==v.Color,"Perlin changed selection/color");for(unsigned uv=0;uv<2;++uv)for(unsigned c=0;c<2;++c)check(a.UV[uv][c]==v.UV[uv][c],"Perlin changed UV");}
}
void controlled() {
  mm::GenMinMesh source;source.Vertices.Resize(257);
  for(int i=0;i<source.Vertices.Count;++i){auto& v=source.Vertices[i];v.Pos.Init((i-128)*.125f,(i%7-3)*.25f,(i%13-6)*.17f);v.Select=i&1;v.Color=0xff123456;v.UV[0][0]=i;v.UV[1][1]=-.25f;}
  unsigned cases=0;
  for(unsigned mode=0;mode<3;++mode)for(unsigned variation=0;variation<8;++variation) {
    std::array<std::uint32_t,13> p{mode,f(variation*.25f),f(-2),f(1.5f),f(.03f*variation),f(.1f),0,f(-256),f(.5f),f(512),f(.7f),f(-.2f),f(0)};
    auto result=mm::GenMinMesh(source);mm::MinMesh_Perlin(&result,mode,transform(p),{as_float(p[10]),as_float(p[11]),as_float(p[12]),0});compare(source,result,p);++cases;
  }
  std::array<std::uint32_t,13> p{0,f(1),f(1),f(1),0,0,0,0,0,0,f(1),f(1),f(1)};
  for(auto rounding:{FE_TONEAREST,FE_UPWARD,FE_DOWNWARD}) {
    auto result=mm::GenMinMesh(source);std::fesetround(rounding);std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
    mm::MinMesh_Perlin(&result,0,transform(p),{1,1,1,0});check(std::fegetround()==rounding,"Perlin leaked rounding mode");check(std::fetestexcept(FE_ALL_EXCEPT)==FE_DIVBYZERO,"Perlin leaked FP exception flags");
    auto invalid=transform(p);invalid.v[6]=std::numeric_limits<float>::infinity();bool caught=false;
    try{mm::MinMesh_Perlin(&result,0,invalid,{1,1,1,0});}catch(const std::runtime_error&){caught=true;}check(caught && std::fegetround()==rounding,"Perlin overflow or rounding restoration");
  }
  std::fesetround(FE_TONEAREST);std::feclearexcept(FE_ALL_EXCEPT);
  std::cout<<cases<<" transforms/selection modes over 257 vertices match archived GenMesh Perlin\n";
}
void debris(const char* path) {
  const auto p=read_project(path);const auto g=connect(p);unsigned count=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0x124) {
    std::cout<<"Checking Debris MINMESH Perlin "<<id<<std::endl;
    const auto input=evaluate_mesh(p,g,g.nodes[id].inputs[0]),output=evaluate_mesh(p,g,id);compare(*input->min_kernel,*output->min_kernel,p.operators[id].parameters);
    std::vector<std::uint8_t> mask;for(int i=0;i<output->min_kernel->Faces.Count;++i){const auto& face=output->min_kernel->Faces[i];mask.push_back(face.Cluster && face.Count>=3 && !(face.Flags&1));}
    check_minmesh_shadow_reference(*output,mask);++count;
  }
  check(count==8,"missing saved MINMESH Perlin graphs");std::cout<<count<<" complete saved Perlin graphs match archived displacement and shadow topology\n";
}
}
int main(int argc,char** argv){try{controlled();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
