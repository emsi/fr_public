// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/scene.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void near(float a,float b,const char* message) {check(std::abs(a-b)<.0001f,message);}
std::uint32_t f(float v) {return std::bit_cast<std::uint32_t>(v);}
template<class F> void rejects(F fn) {bool caught=false;try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,"unsupported scene accepted");}
std::vector<std::uint32_t> srt(float tx=0,float ty=0,float tz=0) {return {f(1),f(1),f(1),0,0,0,f(tx),f(ty),f(tz)};}
struct Fixture {
  wz3::Project p;wz3::Graph g;
  std::size_t add(unsigned command,std::vector<std::uint32_t> parameters={},std::vector<std::size_t> inputs={},std::vector<std::optional<std::size_t>> links={}) {
    wz3::Operator op;op.command=command;op.parameters=std::move(parameters);p.operators.push_back(std::move(op));g.nodes.push_back({true,true,std::move(inputs),std::move(links)});return p.operators.size()-1;
  }
  std::size_t cube() {return add(0x81,{1,1,1,1,f(1),f(1),f(1),0,0,0,0,0,0});}
  std::size_t viewport(std::size_t input) {return add(0xf0,{3,3,0xff000000,0,0,0,0,0,f(-5),f(4096),f(.125f),0,0,f(1),f(1),0xff000000,f(4096),f(16)},{input});}
  void animate(std::size_t id,unsigned variable,unsigned offset,unsigned mask=1) {
    wz3::Animation load,store;load.fields={0,0,3,2,variable,0,0,0};store.fields={0,1,3,0x90|mask,offset,5,0,0};p.operators[id].animations={load,store};
  }
};
void composition() {
  Fixture fxt;const auto cube=fxt.cube();auto child_srt=srt(2);child_srt[0]=f(2);
  auto parent_srt=srt(5);parent_srt[0]=f(3);parent_srt[5]=f(.25f);
  const auto child=fxt.add(0xc3,child_srt,{cube});const auto parent=fxt.add(0xc3,parent_srt,{child});const auto root=fxt.viewport(parent);
  wz3::ScenePlayer player(fxt.p,fxt.g);const auto frame=player.viewport(root,wz3::frame_environment(0),1);
  check(frame.instances.size()==1,"transform instance count");const auto point=wz3::scene_point(frame.instances[0].world,{1,0,0});
  near(point[0],5,"parent translation follows local transform");near(point[1],12,"local scale and translation precede parent scale and rotation");near(point[2],0,"transform plane");
  check(frame.lights.size()==1 && frame.lights[0].color==0x78787878 && frame.lights[0].range==32,"Theta fallback camera light");
  near(frame.lights[0].position[2],-5,"fallback light follows saved camera");
  auto event_world=wz3::scene_srt(srt(100));const auto moved=player.viewport(root,wz3::frame_environment(0),1,event_world);
  near(moved.instances[0].world[12],frame.instances[0].world[12]+100,"event matrix applies to scene instances");
  check(moved.camera.world==frame.camera.world,"viewport camera does not inherit event model transform");
  const auto bounds=wz3::scene_bounds(frame);near(bounds.min[0],4.5f,"world bounds X");near(bounds.min[1],3,"world bounds Y minimum");near(bounds.max[1],9,"world bounds Y maximum");
  auto camera=wz3::SceneCamera{};const auto projection=wz3::scene_view_projection(camera,640,480);
  near(projection[0],1,"projection horizontal zoom");near(projection[5],1,"projection vertical zoom");near(projection[11],1,"positive Z perspective divide");
  near(projection[10],1,"infinite far plane");near(projection[14],-.25f,"D3D-to-GL near depth");near(projection[12],-1/640.0f,"original horizontal pixel shift");
  near((projection[10]*.125f+projection[14])/.125f,-1,"near plane maps to GL negative one");
  camera.released_projection=true;
  const auto released=wz3::scene_view_projection(camera,640,480);
  check(released[12]==0 && released[13]==0,"released projection has no archived constant pixel shift");
  check(released==wz3::scene_view_projection(camera,1024,768),"released geometric projection depends on raster resolution");
  const auto raster=wz3::scene_raster_projection(camera,640,480);
  for(float z:{.125f,1.f,4.f,128.f}) {
    near((raster[8]*z+raster[12])/z*320,.5f,"D3D-to-GLES horizontal sample-center conversion");
    near((raster[9]*z+raster[13])/z*240,-.5f,"D3D-to-GLES vertical sample-center conversion");
  }
  fxt.p.runtime_graph=true;wz3::ScenePlayer runtime(fxt.p,fxt.g);
  check(runtime.viewport(root,wz3::frame_environment(0),1).camera.released_projection,"runtime camera projection profile was lost");
  check(!frame.camera.released_projection,"editor camera acquired release projection semantics");
}
void released_camera_oracle(const char* path) {
  std::ifstream in(path);std::string magic;unsigned cases=0,exact=0,total=0;float maximum=0;
  in>>magic>>cases;check(in.good() && magic=="theta-camera-v1" && cases>0 && cases<=10000,"invalid camera oracle");
  for(unsigned trial=0;trial<cases;++trial) {
    unsigned id=0,sample=0,width=0,height=0;std::uint32_t aspect=0;
    std::vector<std::uint32_t> p(18);std::array<std::uint32_t,22> camera{};std::array<std::uint32_t,16> projection{},vp{};
    in>>id>>sample>>width>>height>>aspect;for(auto& v:p)in>>v;for(auto& v:camera)in>>v;
    for(auto& v:projection)in>>v;for(auto& v:vp)in>>v;check(in.good(),"truncated camera oracle");
    Fixture x;x.p.runtime_graph=true;const auto empty=x.add(0xc1);const auto view=x.add(0xf0,p,{empty});
    wz3::ScenePlayer player(x.p,x.g);const auto scene=player.viewport(view,wz3::frame_environment(0),wz3::as_float(aspect));
    const auto compare=[&](float actual,float expected) {
      check(std::isfinite(actual) && std::isfinite(expected),"nonfinite camera oracle value");
      const auto error=std::abs(actual-expected);maximum=std::max(maximum,error);exact+=f(actual)==f(expected);++total;
      if(error>2e-4f+2e-6f*std::abs(expected))throw std::runtime_error("original camera mismatch: viewport "+std::to_string(id)+
        ", sample "+std::to_string(sample)+", expected "+std::to_string(expected)+", got "+std::to_string(actual));
    };
    for(unsigned c=0;c<16;++c)compare(scene.camera.world[c],wz3::as_float(camera[c]));
    const std::array<float,6> fields{scene.camera.zoom_x,scene.camera.zoom_y,scene.camera.center_x,scene.camera.center_y,scene.camera.near_clip,scene.camera.far_clip};
    for(unsigned c=0;c<6;++c)compare(fields[c],wz3::as_float(camera[c+16]));
    auto identity=scene.camera;identity.world=wz3::scene_identity();
    const auto projected=wz3::scene_view_projection(identity,width,height),combined=wz3::scene_view_projection(scene.camera,width,height);
    for(unsigned c=0;c<16;++c) {
      const auto gl_depth=[&](const auto& matrix){return c%4==2 ? 2*wz3::as_float(matrix[c])-wz3::as_float(matrix[c+1]) : wz3::as_float(matrix[c]);};
      compare(projected[c],gl_depth(projection));compare(combined[c],gl_depth(vp));
    }
  }
  in>>std::ws;check(in.eof(),"trailing camera oracle data");
  std::cout<<cases<<" original camera/view/projection cases match; "<<exact<<'/'<<total<<" components bitwise equal; max absolute error "<<maximum<<'\n';
}
void animation_and_instances() {
  Fixture fxt;const auto cube=fxt.cube();const auto child=fxt.add(0xc3,srt(),{cube});fxt.animate(child,3,6,2);
  auto repeated=srt(2);repeated.push_back(3);const auto multiply=fxt.add(0xc2,repeated,{child});
  const auto added=fxt.add(0xc1,{}, {multiply,child});const auto outer=fxt.add(0xc3,srt(10),{added});const auto root=fxt.viewport(outer);
  wz3::ScenePlayer player(fxt.p,fxt.g);auto env=wz3::frame_environment(0);env.variables[3].fill(99);
  const auto frame=player.viewport(root,env,4.0f/3);check(frame.instances.size()==4,"Multiply and sibling instance count");
  for(unsigned i=0;i<3;++i) {
    near(frame.instances[i].world[12],10+2*i,"Multiply starts at identity and accumulates local transforms");
    near(frame.instances[i].world[13],i,"child animation reads per-instance selection");
    check(frame.instances[i].mesh==frame.instances[3].mesh,"instances must share immutable geometry");
  }
  near(frame.instances[3].world[13],99,"Multiply restores selection before the next sibling");
  near(frame.camera.zoom_y,4/3.0f,"viewport applies screen aspect to vertical zoom");
  const auto again=player.viewport(root,env,1);check(again.instances[0].mesh==frame.instances[0].mesh,"static mesh cache persists between frames");

  Fixture timed;const auto mesh=timed.cube();const auto transform=timed.add(0xc3,srt(),{mesh});timed.animate(transform,0,6);
  const auto view=timed.viewport(transform);timed.animate(view,0,6);
  const auto alias=timed.add(3,{}, {view});const auto cyclic=timed.add(3);timed.g.nodes[cyclic].inputs={cyclic};
  const auto unsupported=timed.add(0xc5);const auto badview=timed.viewport(unsupported);
  wz3::ScenePlayer clock(timed.p,timed.g);const auto a=clock.viewport(alias,wz3::frame_environment(16384),1),b=clock.viewport(alias,wz3::frame_environment(49152),1);
  near(a.instances[0].world[12],.25f,"animated mesh instance at quarter time");near(b.instances[0].world[12],.75f,"animated mesh instance at three-quarter time");
  near(a.camera.world[12],.25f,"animated camera translation");near(b.camera.world[12],.75f,"camera changes between samples");
  check(timed.p.operators[transform].parameters[6]==0,"scene animation mutated project parameters");
  rejects([&]{clock.viewport(cyclic,wz3::frame_environment(0),1);});rejects([&]{clock.viewport(badview,wz3::frame_environment(0),1);});
  check(clock.viewport(alias,wz3::frame_environment(0),1).instances.size()==1,"failed traversal poisoned future evaluation");
}
void lights() {
  Fixture fxt;const auto point=fxt.add(0xc4,{0,0,0,f(2),0,0,0,0,0xff804020,f(3),0});
  const auto direction=fxt.add(0xc4,{0,0,0,f(2),0,0,4,0,0xffffffff,f(2),0});
  const auto added=fxt.add(0xc1,{}, {point,direction});auto transform=srt(1,2,3);transform[0]=transform[1]=transform[2]=f(2);
  const auto root=fxt.viewport(fxt.add(0xc3,transform,{added}));wz3::ScenePlayer player(fxt.p,fxt.g);
  const auto frame=player.viewport(root,wz3::frame_environment(0),1);check(frame.lights.size()==2,"explicit lights suppress fallback");
  check(frame.lights[0].position==wz3::Vec3{5,2,3} && frame.lights[0].direction==wz3::Vec3{0,0,2},"light transform stack");
  check(frame.lights[0].range==1e15f && frame.lights[0].amplify==3 && frame.lights[0].color==0xff804020,"light parameters");
  check(frame.lights[1].position==wz3::Vec3{0,0,2e6f},"unbounded directional light uses distant direction position");
}
void selection_and_passes() {
  Fixture fxt;const auto cube=fxt.cube();const auto left=fxt.add(0xc3,srt(-2),{cube}),right=fxt.add(0xc3,srt(2),{cube});
  const auto selected=fxt.add(0x185,{f(1)},{left,right});const auto pass=fxt.add(0x180,{3},{selected});
  const auto ambient=fxt.add(0x184,{0xff204080});const auto root=fxt.viewport(fxt.add(0xc1,{}, {pass,ambient}));
  const auto alias=fxt.add(3,{}, {cube});wz3::Animation load,store;load.fields={0,0,3,2,0,0,0,0};store.fields={0,1,3,0x8f,3,5,0,0};fxt.p.operators[alias].animations={load,store};
  const auto bad=fxt.viewport(alias);wz3::ScenePlayer player(fxt.p,fxt.g);const auto frame=player.viewport(root,wz3::frame_environment(0),1);
  check(frame.instances.size()==1 && frame.instances[0].world[12]==2 && frame.instances[0].pass_adjust==3,"scene selection and pass adjustment");
  check(frame.ambient==0xff204080,"scene ambient metadata");rejects([&]{player.viewport(bad,wz3::frame_environment(0),1);});
}
void markers() {
  Fixture fxt;const auto cube=fxt.cube(),inner=fxt.add(0x182,{3},{cube});
  const auto outer=fxt.add(0x182,{3},{fxt.add(0xc3,srt(2,3,4),{inner})});
  const auto root=fxt.viewport(fxt.add(0xc3,srt(10),{outer}));
  const auto empty=fxt.viewport(fxt.add(0xc1));const auto only=fxt.viewport(fxt.add(0x182,{31}));
  const auto invalid=fxt.viewport(fxt.add(0x182,{32}));
  wz3::ScenePlayer player(fxt.p,fxt.g);
  for(const auto& m:player.markers())check(m==wz3::scene_identity(),"markers must initialize to identity");
  const auto frame=player.viewport(root,wz3::frame_environment(0),1);
  check(frame.instances.size()==1 && player.markers()[3]==frame.instances[0].world,"nested marker must store before executing its child");
  near(player.markers()[3][12],12,"marker parent composition");near(player.markers()[3][13],3,"marker local translation");
  const auto saved=player.markers()[3];player.begin_frame(100);player.viewport(empty,wz3::frame_environment(65536),1);
  check(player.markers()[3]==saved,"frame/viewport boundary reset marker");
  player.begin_frame(0);player.viewport(only,wz3::frame_environment(0),1,wz3::scene_srt(srt(20)));
  check(player.markers()[3]==saved && player.markers()[31][12]==20,"backward time or childless marker lost state");
  rejects([&]{player.viewport(invalid,wz3::frame_environment(0),1);});
}
void empty_scene() {
  Fixture x;auto p=srt(15);p.push_back(0);
  const auto cube=x.cube(),empty=x.add(0xc0,p),full=x.add(0xc0,p,{cube});
  const auto root=x.viewport(x.add(0xc1,{}, {empty,full}));
  const auto invalid=x.viewport(x.add(0xc0,p,{cube,cube}));
  const auto empty_transform=x.viewport(x.add(0xc3,srt()));
  wz3::ScenePlayer player(x.p,x.g);const auto frame=player.viewport(root,wz3::frame_environment(0),1);
  check(frame.instances.size()==1 && frame.instances[0].world[12]==15,"Scene must accept an optional mesh and transform a present mesh");
  rejects([&]{player.viewport(invalid,wz3::frame_environment(0),1);});
  rejects([&]{player.viewport(empty_transform,wz3::frame_environment(0),1);});
}
void theta(const char* file) {
  const auto p=wz3::read_project(file);const auto g=wz3::connect(p);const wz3::SplineLibrary splines(p);wz3::ScenePlayer player(p,g);
  auto env=wz3::frame_environment(56*65536);const wz3::EventAnimationScope event(p.events[1],56*65536,splines,env);
  const auto frame=player.viewport(590,env,4.0f/3,wz3::scene_srt(p.events[1].transform));
  check(frame.instances.size()==7 && frame.lights.size()==8,"Theta generation2 scene topology");
  check(frame.instances[2].mesh==frame.instances[3].mesh && frame.instances[3].mesh==frame.instances[4].mesh,"Theta Multiply geometry sharing");
  near(frame.camera.world[12],-5.252972126f,"saved Theta camera X midpoint");near(frame.camera.world[14],-9.3050251f,"saved Theta camera Z midpoint");
  for(auto id:{590u,623u,719u,723u,857u,1111u,1728u})check(!player.viewport(id,wz3::frame_environment(32768),4.0f/3).instances.empty(),"supported Theta scene is empty");
  const auto particles=player.viewport(697,wz3::frame_environment(32768),1);
  check(particles.instances.size()==333,"Theta generation3 must include both 150-particle branches");
}
}
int main(int argc,char** argv) {
  try {if(argc==3 && std::string_view(argv[1])=="--release-camera-oracle") {released_camera_oracle(argv[2]);return 0;}
    check(argc==2,"expected Theta project");composition();animation_and_instances();lights();selection_and_passes();markers();empty_scene();theta(argv[1]);
    std::cout<<"scene transforms, instancing, scoped animation, lights and Theta viewports passed\n";
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
