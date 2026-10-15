// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/spline3d.hpp"
#include "spline_kernel.hpp"
#include <algorithm>
#include <cfenv>
#include <iostream>
#include <limits>
namespace source {
using wz3::legacy::sInt;using wz3::legacy::sF32;using wz3::legacy::sVector;using wz3::legacy::sMatrix;
using wz3::legacy::BlobSpline;using wz3::legacy::sSetMem;using sU16=std::uint16_t;
double sFPow(double a,double b){return std::pow(a,b);}
float wave_form[128][1024],wave_center[128][2];
struct Camera {sMatrix ModelSpace;};
struct Shader {unsigned calls{};void Set(Camera&){++calls;}};
struct GenMaterial {struct Pass {Shader* Mtrl;};struct List {int Count=1;Pass pass;Pass& operator[](int i){check(i==0,"Water must use only material pass zero");return pass;}} Passes;};
struct GenSpline {BlobSpline* spline;BlobSpline* GetBlobSpline(){return spline;}};
struct KOp {void* Cache{};GenMaterial* material{};KOp* input{};void* GetLinkCache(int){return material;}KOp* GetInput(int){return input;}};
struct KEnvironment {std::array<sVector,32> Var;Camera CurrentCam;struct Stack {sMatrix model;sMatrix Top(){return model;}} ExecStack;};
constexpr unsigned KV_TIME=0,sFVF_TSPACE=1,sGEO_TRI=1;
void sQuad(sU16*& p,int a,int b,int c,int d){for(int i:{a,b,c,a,c,d})*p++=sU16(i);}
struct System {
  std::vector<float> vertices;std::vector<sU16> indices;unsigned draws{};
  int GeoAdd(int,int){return 1;}
  void GeoBegin(int,int nv,int ni,float** fp,void** ip){vertices.resize(nv*16);indices.resize(ni);*fp=vertices.data();*ip=indices.data();}
  void GeoEnd(int){}void GeoDraw(int){++draws;}void GeoRem(int){}
} system;
System* sSystem=&system;
#include "water_oracle.inc"
void compare(const wz3::WaterEffect& water) {
  auto blob=*water.spline;GenSpline spline{&blob};Shader shader;GenMaterial material{{2,{&shader}}};KOp input{&spline},op{nullptr,&material,&input};
  KEnvironment env;env.Var[KV_TIME].x=water.time;system.vertices.clear();system.indices.clear();system.draws=0;
  const auto r=[&](unsigned i){return wz3::as_float(water.parameters[i]);};
  Exec_Effect_Water(&op,&env,r(0),r(1),r(2),r(3),r(4),water.parameters[5]);
  const auto actual=wz3::water_geometry(water);
  check(shader.calls==1 && system.draws==1,"archived Water draw count");
  check(actual.indices==system.indices,"Water triangle winding/index mismatch");
  check(actual.vertices.size()==system.vertices.size(),"Water vertex count mismatch");
  for(std::size_t i=0;i<actual.vertices.size();++i)if(f(actual.vertices[i])!=f(system.vertices[i]))
    throw std::runtime_error("Water differs from archived handler at float slot "+std::to_string(i));
}
}
namespace {
using namespace wz3;
std::shared_ptr<legacy::BlobSpline> waves() {
  auto out=std::make_shared<legacy::BlobSpline>();
  out->Keys={{0,.1f,-.1f,0,.1f,2,2.5f,1.25f,.05f},{0,.35f,.1f,0,-.15f,1.25f,.65f,2.13f,.04f}};
  out->Count=out->Keys.size();return out;
}
WaterEffect fixture(unsigned grid=4) {return {{f(8),f(1),f(3),f(1),f(1),grid},waves(),.5f};}
void oracle() {
  unsigned count=0;
  for(unsigned grid:{1u,3u,7u,8u})for(float time:{-.5f,.1f,.2f,.5f,2.f,100.f}) {
    auto water=fixture(grid);water.time=time;source::compare(water);++count;
  }
  for(float position:{-10.f,-1.f,0.f,1.f,10.f}) {
    auto water=fixture();auto spline=waves();spline->Keys[0].px=position;water.spline=spline;source::compare(water);++count;
  }
  std::cout<<count<<" Water geometry cases match every archived vertex word and index\n";
}
template<class F> void rejects(F action){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid Water accepted");}
void boundaries() {
  auto water=fixture(8);water.time=-1;const auto flat=water_geometry(water);
  check(flat.vertices.size()==65536*16 && *std::max_element(flat.indices.begin(),flat.indices.end())==65535,"Water maximum grid/index");
  for(std::size_t i=0;i<flat.vertices.size();i+=16) {
    check(flat.vertices[i+1]==0 && flat.vertices[i+3]==0 && flat.vertices[i+4]==1 && flat.vertices[i+5]==0,"flat Water position/normal");
    check(f(flat.vertices[i+6])==0xffffaa88 && f(flat.vertices[i+7])==0xff88aaff,"Water packed colors");
    for(unsigned c=10;c<16;++c)check(flat.vertices[i+c]==0,"Water tangents must remain zero");
  }
  near(flat.vertices[0],-4,"Water left edge");near(flat.vertices[2],-4,"Water near edge");
  near(flat.vertices[flat.vertices.size()-16],4,"Water right edge");near(flat.vertices[flat.vertices.size()-8],3,"Water last U");
  auto spline=waves();spline->Keys.assign(129,spline->Keys[0]);spline->Count=129;water.spline=spline;
  water_geometry(water);water.time=.5f;rejects([&]{water_geometry(water);});
  for(unsigned kind=0;kind<8;++kind) {
    water=fixture();spline=waves();water.spline=spline;
    if(kind==0)water.parameters[5]=0;if(kind==1)water.parameters[5]=9;
    if(kind==2)water.parameters[0]=0;if(kind==3)water.parameters[1]=f(std::numeric_limits<float>::infinity());
    if(kind==4)spline->Keys[0].rz=0;if(kind==5)spline->Count=1;
    if(kind==6)water.time=std::numeric_limits<float>::quiet_NaN();if(kind==7)water.spline.reset();
    rejects([&]{water_geometry(water);});
  }
  const int old=std::fegetround();const auto expected=water_geometry(fixture());std::fesetround(FE_DOWNWARD);
  const auto actual=water_geometry(fixture());check(std::fegetround()==FE_DOWNWARD,"Water changed caller rounding");
  for(std::size_t i=0;i<actual.vertices.size();++i)check(f(actual.vertices[i])==f(expected.vertices[i]),"Water depends on caller rounding");
  rejects([&]{water_geometry(water);});check(std::fegetround()==FE_DOWNWARD,"Water exception changed caller rounding");std::fesetround(old);
}
void dispatch() {
  FrameFixture x;const auto curve=x.add(0x18);std::vector<std::uint32_t> blob(16);blob[0]=2;blob[1]=3;blob[6]=f(10);blob[7]=f(1);
  for(float time:{0.f,1.f})blob.insert(blob.end(),{0,f(time),0,0,0,f(2),f(2.5f),f(1.25f),f(.05f)});
  auto& source=x.p.operators[curve];source.blob_offset=x.p.bytes.size();source.blob_size=blob.size()*4;
  for(auto word:blob)for(unsigned i=0;i<4;++i)x.p.bytes.push_back(std::uint8_t(word>>(i*8)));
  const auto red=x.material(0xffff0000),blue=x.material(0xff0000ff),material=x.add(0xd1,{}, {red,blue});x.animate(red,0,24);
  const auto water=x.add(0x73,{f(8),f(1),f(1),f(1),f(1),3},{curve},{material});
  const auto transform=x.add(0xc3,{f(2),f(1),f(1),0,0,0,f(3),0,0},{water}),view=x.viewport();x.g.nodes[view].inputs={transform};
  ScenePlayer player(x.p,x.g);
  for(float time:{.25f,.75f}) {
    auto env=frame_environment(0);env.variables[0].fill(time);const auto frame=player.viewport(view,env,1);
    check(frame.effects.size()==1 && frame.effects[0].water,"Water dispatch count");const auto& draw=frame.effects[0];
    check(draw.shader.words[28]==0xffff0000 && draw.shader.words[24]==f(0),"Water must use only the first cached pass without executing its material link");
    near(draw.water->time,time,"Water must use current effect time");near(draw.material_model[0],2,"Water model scale");near(draw.material_model[12],3,"Water model translation");
  }
  x.g.nodes[water].links={std::nullopt};ScenePlayer no_material(x.p,x.g);
  check(no_material.viewport(view,frame_environment(0),1).effects.empty(),"materialless Water must not draw");
}
void debris(const char* path) {
  const auto p=read_project(path);const auto g=connect(p);SpatialSplines splines(p,g);unsigned count=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0x73) {
    WaterEffect effect;const auto& parameters=p.operators[id].parameters;check(parameters.size()==6,"saved Water layout");
    std::copy(parameters.begin(),parameters.end(),effect.parameters.begin());auto environment=frame_environment(0);effect.spline=splines.static_curve(g.nodes[id].inputs[0],environment);
    for(float time:{0.f,.49f,.5f,.81f,.85f,1.f,1.5f}){effect.time=time;source::compare(effect);}++count;
  }
  check(count==1,"Debris reachable Water count changed");
  TextureOptions options;
  // Read only Water's scene graph here; the complete final frame is checked by
  // the GPU and continuous timeline runs.
  auto env=frame_environment(0);env.variables[0].fill(.85f);
  auto local_p=p;auto local_g=g;FrameFixture view;const auto viewport=view.viewport();
  auto op=view.p.operators[viewport];const auto vp=local_p.operators.size();local_p.operators.push_back(op);local_g.nodes.push_back({true,true,{6001},{}});
  ScenePlayer local(local_p,local_g,options);const auto scene=local.viewport(vp,env,1);
  check(scene.effects.size()==1 && scene.effects[0].water && scene.effects[0].vertices.empty(),"scene must defer Water geometry until drawing");
  near(scene.effects[0].water->time,.85f,"scene Water time");
  const auto cached=MaterialRuntime(p,g).snapshot(6036).passes[0].shader;
  check(scene.effects[0].shader.words==cached.words && scene.effects[0].shader.textures==cached.textures,"Water did not retain its first cached material pass");
  std::cout<<count<<" saved Debris Water recipe matches at seven times; scene dispatch retains cached material and time\n";
}
}
int main(int argc,char** argv){try{check(argc==2,"expected Debris project");oracle();boundaries();dispatch();debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
