// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "minmesh_reference.hpp"
#include "spline_kernel.hpp"
#include "wz3/spline3d.hpp"
#include <cfenv>
#include <iostream>
namespace {
using namespace wz3;namespace mm=legacy::minimal;
template<class F> void rejects(F action){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid Pipe accepted");}
struct Fixture:FrameFixture {
  std::size_t pipe(bool bent=true) {
    const auto id=add(0x1a);std::vector<std::uint32_t> words(16);words[0]=bent ? 3 : 1;words[1]=1;
    for(const auto& k:(bent ? std::vector<std::array<float,4>>{{0,0,8,2},{8,0,8,2},{8,0,16,0}} : std::vector<std::array<float,4>>{{0,0,8,0}}))
      words.insert(words.end(),{f(k[0]),f(k[1]),f(k[2]),f(k[3]),0,0,0,0});
    p.operators[id].blob_offset=p.bytes.size();p.operators[id].blob_size=words.size()*4;
    for(auto word:words)for(unsigned i=0;i<4;++i)p.bytes.push_back(std::uint8_t(word>>(8*i)));
    return id;
  }
};
std::unique_ptr<mm::GenMinMesh> probe(unsigned tag,float length) {
  auto m=std::make_unique<mm::GenMinMesh>();m->Vertices.Resize(3);m->Faces.Resize(1);m->Clusters[1].Id=tag;m->Clusters[1].RenderPass=tag+5;
  for(unsigned i=0;i<3;++i){auto& v=m->Vertices[i];v.Pos.Init(0,.125f,i*length/2);v.Color=tag;v.UV[0][0]=.125f;v.UV[0][1]=.375f;v.UV[1][0]=-1;v.UV[1][1]=2;v.Select=1;}
  auto& face=m->Faces[0];face.Count=3;face.Cluster=1;face.Select=1;face.Flags=1;for(unsigned i=0;i<3;++i)face.Vertices[i]=i;
  return m;
}
void geometry() {
  Fixture fixture;const auto id=fixture.pipe();SpatialSplines library(fixture.p,fixture.g);auto env=frame_environment(0);auto [curve,pipe]=library.pipe_curve(id,env);
  auto straight=probe(1,2),curved=probe(2,4),ring=probe(3,0);unsigned cases=0;
  for(unsigned flags=0;flags<32;++flags)for(bool custom:{false,true})for(bool rings:{false,true}) {
    const std::array<std::uint32_t,4> p{flags,f(2),f(2),f(2)};
    auto mesh=std::unique_ptr<mm::GenMinMesh>(mm::pipe_mesh(*curve,*pipe,*straight,custom ? curved.get() : nullptr,rings ? ring.get() : nullptr,p));
    unsigned at=0,faces=0;float offset=0;
    const auto verify=[&](unsigned tag,const std::array<Vec3,3>& positions,const std::array<float,3>& uv) {
      for(unsigned i=0;i<3;++i){const auto& v=mesh->Vertices[at+i];const Vec3 pos{v.Pos.x,v.Pos.y,v.Pos.z};for(unsigned c=0;c<3;++c)near(pos[c],positions[i][c],"Pipe position differs from analytic path");
        near(v.UV[0][1],uv[i],"Pipe longitudinal texture mapping");check(v.Color==tag && v.Select==1 && v.UV[0][0]==.125f && v.UV[1][0]==-1 && v.UV[1][1]==2,"Pipe changed source attributes");}
      const auto& face=mesh->Faces[faces++];check(face.Count==3 && face.Select==1 && face.Flags==1,"Pipe changed source polygon flags");
      for(unsigned i=0;i<3;++i)check(face.Vertices[i]==int(at+i),"Pipe polygon offset");
      const auto& cluster=mesh->Clusters[face.Cluster];check(cluster.Id==int(tag) && cluster.RenderPass==int(tag+5),"Pipe cluster binding lost");at+=3;
    };
    const auto position=[](unsigned seg,float distance){return seg==0 ? Vec3{0,.125f,distance} : seg==1 ? Vec3{2+distance,.125f,8} : Vec3{8,.125f,10+distance};};
    for(unsigned seg=0;seg<3;++seg) {
      if(seg) {
        std::array<Vec3,3> positions;std::array<float,3> uv;
        for(unsigned i=0;i<3;++i){const auto angle=i*legacy::sPI/4;positions[i]=seg==1 ? Vec3{2-2*std::cos(angle),.125f,6+2*std::sin(angle)} : Vec3{6+2*std::sin(angle),.125f,10-2*std::cos(angle)};
          uv[i]=(flags&17)==17 ? offset+i*legacy::sPI : .375f;}
        verify(custom ? 2 : 1,positions,uv);if((flags&24)==24)offset+=2*legacy::sPI;
      }
      const float length=seg==1 ? 4 : 6;const unsigned repeats=(flags&4) ? unsigned(length/2) : 1;
      for(unsigned n=0;n<repeats;++n){std::array<Vec3,3> positions;std::array<float,3> uv;
        for(unsigned i=0;i<3;++i){const auto distance=(n+i*.5f)*length/repeats;positions[i]=position(seg,distance);uv[i]=(flags&1) ? offset+distance*2 : .375f;}verify(1,positions,uv);}
      if(flags&8)offset+=length*2;
      if(rings){const auto add=[&](float distance){const auto v=position(seg,distance);verify(3,{v,v,v},{.375f,.375f,.375f});};add(0);add(length);
        if(flags&2)for(unsigned i=1;i<unsigned(length/2);++i)add(i*2.f);}
    }
    check(at==unsigned(mesh->Vertices.Count) && faces==unsigned(mesh->Faces.Count),"Pipe piece count");++cases;
  }
  check(straight->Vertices[2].Pos.z==2 && curved->Vertices[2].Pos.z==4,"Pipe mutated borrowed source geometry");
  const auto scaled=fixture.add(0x1c,{f(1),f(1),f(1),0},{id});SpatialSplines scaled_library(fixture.p,fixture.g);
  rejects([&]{scaled_library.pipe_curve(scaled,env);}); // Original SplineScale drops the pipe metadata.
  std::fesetround(FE_DOWNWARD);auto mesh=std::unique_ptr<mm::GenMinMesh>(mm::pipe_mesh(*curve,*pipe,*straight,nullptr,nullptr,{std::array<std::uint32_t,4>{0,f(1),f(1),f(1)}}));
  check(std::fegetround()==FE_DOWNWARD,"Pipe leaked rounding mode");std::fesetround(FE_TONEAREST);
  std::cout<<cases<<" straight/bend/ring combinations match analytic geometry and UVs\n";
}
void limits() {
  Fixture x;const auto id=x.pipe(false);SpatialSplines library(x.p,x.g);auto env=frame_environment(0);auto [curve,pipe]=library.pipe_curve(id,env);auto source=probe(1,2);
  const auto run=[&](unsigned flags,float distance){return std::unique_ptr<mm::GenMinMesh>(mm::pipe_mesh(*curve,*pipe,*source,nullptr,nullptr,std::array<std::uint32_t,4>{flags,f(1),f(1),f(distance)}));};
  check(run(4,.01f)->Vertices.Count==64*3,"Pipe repeat clamp must be 64");
  rejects([&]{run(4,0);});rejects([&]{run(4,1e-30f);});rejects([&]{run(32,1);});
  for(auto& v:source->Vertices.storage)v.Pos.z=0;rejects([&]{run(0,1);});
}
void oblique() {
  Fixture x;const auto id=x.pipe();auto& op=x.p.operators[id];
  const auto word=[&](unsigned index,std::uint32_t value){for(unsigned b=0;b<4;++b)x.p.bytes[op.blob_offset+index*4+b]=std::uint8_t(value>>(b*8));};
  // A 60-degree turn: the saved radius is a setback along each straight,
  // making the actual bend radius 2*sqrt(3). UV length still uses the
  // archived radius*interior-angle expression, not geometric arc length.
  word(0,2);word(24,f(4*std::sqrt(3.f)));word(26,f(12));word(27,0);
  SpatialSplines library(x.p,x.g);auto env=frame_environment(0);auto [curve,pipe]=library.pipe_curve(id,env);auto source=probe(1,2);
  auto out=std::unique_ptr<mm::GenMinMesh>(mm::pipe_mesh(*curve,*pipe,*source,nullptr,nullptr,std::array<std::uint32_t,4>{25,f(-2),f(1),f(1)}));
  const auto radius=2*std::sqrt(3.f);
  for(unsigned i=0;i<3;++i){const auto& v=out->Vertices[3+i];const auto angle=i*legacy::sPI/6;
    near(v.Pos.x,radius*(1-std::cos(angle)),"oblique bend X");near(v.Pos.z,6+radius*std::sin(angle),"oblique bend Z");
    near(v.UV[0][1],-12-i*4*legacy::sPI/3,"oblique bend retains original UV distance");}
}
void debris(const char* path) {
  const auto p=read_project(path);const auto g=connect(p);unsigned count=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0x12c) {
    std::cout<<"Checking Debris Pipe "<<id<<std::endl;const auto mesh=evaluate_mesh(p,g,id);check(!mesh->vertices.empty() && !mesh->faces.empty(),"saved Pipe empty");
    std::vector<std::uint8_t> mask;for(int i=0;i<mesh->min_kernel->Faces.Count;++i){const auto& face=mesh->min_kernel->Faces[i];mask.push_back(face.Cluster && face.Count>=3 && !(face.Flags&1));}
    check_minmesh_shadow_reference(*mesh,mask);++count;
  }
  check(count==31,"missing saved Pipe graphs");std::cout<<count<<" complete saved Pipe graphs build with archived shadow topology\n";
}
}
int main(int argc,char** argv){try{geometry();limits();oblique();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
