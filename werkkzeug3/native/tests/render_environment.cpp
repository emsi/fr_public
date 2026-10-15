// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "minmesh_kernel.hpp"
#include <algorithm>
#include <iostream>
namespace {
using namespace wz3;
legacy::sMatrix matrix(const SceneMatrix& m){legacy::sMatrix out;out.i.Init(m[0],m[1],m[2],m[3]);out.j.Init(m[4],m[5],m[6],m[7]);out.k.Init(m[8],m[9],m[10],m[11]);out.l.Init(m[12],m[13],m[14],m[15]);return out;}
// Execute the coordinate operations from material11.vsh at each vertex, using
// the archived matrix/vector routines. Rasterize the resulting UVs with the
// existing ordinary UV shader to check interpolation as well as generation.
std::array<float,2> reference(const MaterialShader& shader,unsigned slot,const MeshVertex& v,Vec3 normal,const SceneMatrix& model,const SceneCamera& camera) {
  auto eye=matrix(camera.world);eye.TransR();auto world=matrix(model);legacy::sMatrix model_view;model_view.MulA(world,eye);
  legacy::sVector pos,coord,n;pos.Init(v.position[0],v.position[1],v.position[2],1);coord.Init(v.uv[0],v.uv[1],0,1);n.Init(normal[0],normal[1],normal[2],0);
  if(slot==2) {
    const auto mode=(shader.words[1]>>4)&3;
    if(mode==1){n.Rotate3(model_view);coord.x=n.x*.5f+.5f;coord.y=n.y*-.5f+.5f;}
    if(mode==2){
      legacy::sVector e;e.Rotate34(model_view,pos);e.Scale3(1/std::sqrt(e.Dot3(e)));n.Rotate3(model_view);
      coord.w=e.Dot3(n)*(2/n.Dot3(n));coord.x=n.x*coord.w-e.x;coord.y=n.y*coord.w-e.y;coord.z=n.z*coord.w-e.z;
      coord.x=coord.x*.5f+.5f;coord.y=coord.y*-.5f+.5f;
    }
    if(mode==3){coord.x=n.x;coord.y=n.y;coord.z=n.z;}
  }
  if(slot==3)switch((shader.words[1]>>8)&15){case 1:coord=pos;break;case 2:coord.Rotate34(world,pos);break;case 3:coord.Rotate34(model_view,pos);break;}
  const auto real=[&](unsigned i){return as_float(shader.words[i]);};
  switch((shader.words[4+slot]>>12)&3) {
    case 0:return {coord.x,coord.y};
    case 1:return {coord.x*real(24+slot),coord.y*real(24+slot)};
    case 2:{float srt[9];for(unsigned i=0;i<9;++i)srt[i]=real(34+i);legacy::sMatrix m;m.InitSRT(srt);
      return {coord.x*m.i.x+coord.y*m.j.x+coord.z*m.k.x+coord.w*m.l.x,coord.x*m.i.y+coord.y*m.j.y+coord.z*m.k.y+coord.w*m.l.y};}
    default:{const float sn=std::sin(double(real(45))),cs=std::cos(double(real(45)));
      return {coord.x*(real(43)*cs)+coord.y*(real(44)*sn)+coord.w*real(46),coord.x*(-real(43)*sn)+coord.y*(real(44)*cs)+coord.w*real(47)};}
  }
}
std::shared_ptr<Mesh> geometry() {
  auto core=std::make_shared<legacy::minimal::GenMinMesh>();core->Vertices.Resize(4);core->Faces.Resize(1);
  const Vec3 positions[]={{-1.3f,1,3},{1.2f,1,3.5f},{1.2f,-1,3.5f},{-1.3f,-1,3}};
  for(unsigned i=0;i<4;++i){auto& v=core->Vertices[i];v.Pos.Init(positions[i][0],positions[i][1],positions[i][2]);v.UV[0][0]=i==1 || i==2;v.UV[0][1]=i>=2;v.Color=0xffffffff;}
  auto& face=core->Faces[0];face.Count=4;face.Cluster=1;for(unsigned i=0;i<4;++i)face.Vertices[i]=i;
  auto mesh=std::make_shared<Mesh>(legacy::minimal::export_mesh(core));mesh->faces[0].material=0;
  const Vec3 normals[]={{.2f,.4f,-.8f},{-.5f,.1f,-1.4f},{.1f,-.7f,-.8f},{.6f,-.2f,-1.2f}};
  for(unsigned i=0;i<4;++i)core->Vertices[i].Normal.Init(normals[i][0],normals[i][1],normals[i][2]);return mesh;
}
DemoFrame frame(SceneFrame scene,std::size_t target) {
  DemoFrame out;out.width=128;out.height=96;out.surfaces={{0,{128,96}},{target,{64,64}}};out.commands.push_back({0,{0,128,96},std::move(scene)});return out;
}
void pattern(FrameRenderer& renderer,std::size_t target) {
  auto out=frame({},target);out.commands.clear();
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){FrameQuad q;q.rectangle={x/16.f,y/16.f,(x+1)/16.f,(y+1)/16.f};q.shader.words[0]=0x20002;q.shader.words[9]=0x10;q.shader.words[28]=0xff000000|(x*17<<16)|(y*17<<8)|((x*13+y*7)&255);out.commands.push_back({0,{target,64,64},q});}
  renderer.render(out);
}
void compare(FrameRenderer& renderer,SceneFrame scene,std::size_t target,unsigned slot) {
  const auto actual=renderer.render(frame(scene,target));const auto source=scene.instances[0].mesh;
  auto expected_mesh=std::make_shared<Mesh>(*source);expected_mesh->min_kernel.reset();
  auto& shader=scene.materials.at(0).passes[0].shader;
  for(unsigned i=0;i<source->vertices.size();++i){const auto n=source->min_kernel->Vertices[i].Normal;const auto uv=reference(shader,slot,source->vertices[i],{n.x,n.y,n.z},scene.instances[0].world,scene.camera);expected_mesh->vertices[i].uv={uv[0],uv[1],0};}
  shader.words[1]=0;shader.words[4+slot]&=~0x3000u;scene.instances[0].mesh=expected_mesh;
  const auto expected=renderer.render(frame(scene,target));unsigned changed=0,maximum=0;
  for(std::size_t i=0;i<actual.size();++i){maximum=std::max(maximum,unsigned(std::abs(int(actual[i])-expected[i])));if(i%4!=3)changed+=actual[i]!=0;}
  check(changed>200,"generated UV fixture is invisible");
  if(maximum>2)throw std::runtime_error("generated environment/projection UV mismatch: "+std::to_string(maximum));
}
void cases() {
  FrameFixture x;const auto bitmap=x.add(0x26,{6,6,1});const auto target=13+bitmap;FrameRenderer renderer(x.p,x.g);pattern(renderer,target);const auto mesh=geometry();unsigned count=0;
  for(unsigned flags:{0x10u,0x20u,0x30u,0x100u,0x200u,0x300u})for(unsigned transform=0;transform<4;++transform)for(unsigned pose=0;pose<3;++pose) {
    SceneFrame scene;scene.viewport_flags=3;scene.instances={{0,mesh}};const auto slot=flags&0x30 ? 2u : 3u;
    MaterialPass pass;auto& s=pass.shader;auto& w=s.words;w[0]=0x302;w[1]=flags;w[8+(slot==2 ? 9 : 10)]=0x10;w[4+slot]=0x101|(transform<<12);s.textures[slot]=bitmap;
    for(unsigned i=0;i<4;++i)w[24+i]=f(.73f);
    const float srt[]={.31f,.27f,.42f,.07f,.13f,-.11f,.14f,.21f,.03f};for(unsigned i=0;i<9;++i)w[34+i]=f(srt[i]);
    w[43]=f(.7f);w[44]=f(.5f);w[45]=f(.37f);w[46]=f(.13f);w[47]=f(.17f);scene.materials[0].passes={pass};
    if(pose){scene.instances[0].world=scene_srt(std::array<std::uint32_t,9>{f(.8f),f(1.1f),f(.7f),f(.02f),f(.04f),f(.03f),f(.2f),f(-.1f),f(.7f)});}
    if(pose==2)scene.camera.world=scene_srt(std::array<std::uint32_t,9>{f(1),f(1),f(1),f(.02f),f(-.03f),f(.01f),f(-.1f),f(.3f),f(-.2f)});
    compare(renderer,scene,target,slot);++count;
  }
  // Reflection leaves its scalar multiplier in W. SRT translation therefore
  // scales by -2 at this vertex; replacing W with 1 visibly changes the image.
  MaterialShader shader;shader.words[1]=0x20;shader.words[6]=0x3000;shader.words[43]=shader.words[44]=f(1);shader.words[46]=f(.2f);
  MeshVertex v;v.position={0,0,3};const auto uv=reference(shader,2,v,{0,0,-1},scene_identity(),{});near(uv[0],.1f,"reflection homogeneous coordinate lost");near(uv[1],.5f,"reflection anchor");
  // Generated coordinates must not silently fall back to raw quad UVs.
  auto invalid=frame({},target);FrameQuad quad;quad.shader=shader;quad.shader.words[9]=0x10;invalid.commands={{0,{0,128,96},quad}};
  bool rejected=false;try{renderer.render(invalid);}catch(const std::runtime_error&){rejected=true;}check(rejected,"environment quad accepted without normals");
  std::cout<<count<<" GPU environment/projection cases match per-vertex archived coordinate arithmetic\n";
}
void debris(const char* path) {
  const auto p=read_project(path);const auto g=connect(p);MaterialRuntime runtime(p,g);unsigned count=0;
  FrameFixture fixture;const auto bitmap=fixture.add(0x26,{6,6,1});const auto target=13+bitmap;FrameRenderer renderer(fixture.p,fixture.g);pattern(renderer,target);const auto mesh=geometry();
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0xd0) {
    const auto material=runtime.snapshot(id);
    for(auto pass:material.passes)if((pass.shader.words[1]&0xf30) && !(pass.shader.words[2]&7)) {
      const auto slot=pass.shader.words[1]&0x30 ? 2u : 3u;
      bool only=true;for(unsigned i=0;i<4;++i)if(i!=slot && pass.shader.textures[i])only=false;
      if(!only)continue;
      pass.shader.textures[slot]=bitmap;
      // Keep saved coordinate parameters and combiners, isolate visibility
      // from blend/depth/light-insert state in this coordinate test.
      pass.shader.words[0]=0x302;SceneFrame scene;scene.viewport_flags=3;scene.instances={{0,mesh}};scene.materials[0].passes={pass};compare(renderer,scene,target,slot);++count;
    }
  }
  check(count>0,"no Debris generated-coordinate materials checked");std::cout<<count<<" saved Debris environment/projection passes match using controlled texture\n";
}
}
int main(int argc,char** argv){try{cases();if(argc==2)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
