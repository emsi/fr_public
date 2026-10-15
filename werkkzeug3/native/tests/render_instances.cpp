// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "minmesh_kernel.hpp"
#include "wz3/scene_draw.hpp"
#include <numeric>
#include <iostream>
namespace source {
using sInt=int;using sMatrix=wz3::legacy::sMatrix;
struct sVertexTSpace3Big {float x,y,z,nx,ny,nz,sx,sy,sz;std::uint32_t c;float u,v;};
static_assert(sizeof(sVertexTSpace3Big)==48);
#include "instances_oracle.inc"
}
namespace {
using namespace wz3;
SceneMatrix srt(float x,float y,float z,float angle=0,float scale=1) {
  return scene_srt(std::array<std::uint32_t,9>{f(scale),f(.9f),f(1.1f),f(angle),f(.03f),f(-angle),f(x),f(y),f(z)});
}
std::shared_ptr<Mesh> prototype() {
  auto core=std::make_shared<legacy::minimal::GenMinMesh>();core->Vertices.Resize(4);core->Faces.Resize(1);
  const Vec3 positions[]={{-.6f,.5f,0},{.6f,.5f,.2f},{.6f,-.5f,.2f},{-.6f,-.5f,0}};
  for(unsigned i=0;i<4;++i){auto& v=core->Vertices[i];v.Pos.Init(positions[i][0],positions[i][1],positions[i][2]);v.UV[0][0]=i==1 || i==2;v.UV[0][1]=i>=2;v.Color=0xff7090b0;}
  auto& face=core->Faces[0];face.Count=4;face.Cluster=1;for(unsigned i=0;i<4;++i)face.Vertices[i]=i;
  auto mesh=std::make_shared<Mesh>(legacy::minimal::export_mesh(core));mesh->faces[0].material=0;
  // These intentionally nonunit, varying normals expose accidental normalization
  // or moving light/eye coordinates into each instance's space.
  for(unsigned i=0;i<4;++i)core->Vertices[i].Normal.Init(.1f*float(i),.05f,-.8f-.1f*i);
  return mesh;
}
std::shared_ptr<const Mesh> expanded(const Mesh& mesh,const std::vector<SceneMatrix>& matrices) {
  const auto& core=*mesh.min_kernel;std::vector<source::sVertexTSpace3Big> vertices;
  for(int i=0;i<core.Vertices.Count;++i){const auto& v=core.Vertices[i];vertices.push_back({v.Pos.x,v.Pos.y,v.Pos.z,v.Normal.x,v.Normal.y,v.Normal.z,v.Tangent.x,v.Tangent.y,v.Tangent.z,v.Color,v.UV[0][0],v.UV[0][1]});}
  std::vector<int> indices(vertices.size());std::iota(indices.begin(),indices.end(),0);
  std::vector<source::sMatrix> poses;
  for(const auto& m:matrices){source::sMatrix out;out.i.Init(m[0],m[1],m[2],m[3]);out.j.Init(m[4],m[5],m[6],m[7]);out.k.Init(m[8],m[9],m[10],m[11]);out.l.Init(m[12],m[13],m[14],m[15]);poses.push_back(out);}
  std::vector<source::sVertexTSpace3Big> output(vertices.size()*poses.size());
  source::InstancingInner(output.data(),vertices.data(),indices.data(),int(vertices.size()),poses.data(),int(poses.size()));
  auto transformed=std::make_shared<legacy::minimal::GenMinMesh>(core);transformed->Vertices.Resize(output.size());transformed->Faces.Resize(core.Faces.Count*poses.size());
  auto result=std::make_shared<Mesh>();result->min_kernel=transformed;
  for(unsigned copy=0;copy<poses.size();++copy) {
    for(unsigned i=0;i<vertices.size();++i) {
      const auto at=copy*vertices.size()+i;const auto& v=output[at];auto& vert=transformed->Vertices[at];vert=core.Vertices[i];
      vert.Pos.Init(v.x,v.y,v.z);vert.Normal.Init(v.nx,v.ny,v.nz);vert.Tangent.Init(v.sx,v.sy,v.sz);
      auto pub=mesh.vertices[i];pub.position={v.x,v.y,v.z};pub.tangent={v.sx,v.sy,v.sz};pub.point=at;result->vertices.push_back(pub);
    }
    for(int i=0;i<core.Faces.Count;++i){auto face=core.Faces[i];for(unsigned c=0;c<face.Count;++c)face.Vertices[c]+=copy*vertices.size();transformed->Faces[copy*core.Faces.Count+i]=face;}
    for(auto face:mesh.faces){for(unsigned c=0;c<face.count;++c)face.vertices[c]+=copy*vertices.size();result->faces.push_back(face);}
  }
  return result;
}
std::vector<std::uint8_t> render(FrameRenderer& renderer,SceneFrame scene) {
  DemoFrame frame;frame.width=128;frame.height=96;frame.surfaces[0]={128,96};frame.commands.push_back({0,{0,128,96},std::move(scene)});return renderer.render(frame);
}
SceneFrame setup(const std::shared_ptr<const Mesh>& mesh,std::shared_ptr<const std::vector<SceneMatrix>> matrices) {
  SceneFrame scene;scene.viewport_flags=3;scene.instances={{0,mesh,srt(.1f,-.1f,.2f,.02f,.85f),0,0,matrices}};
  scene.camera.world=srt(-.1f,.05f,-.4f,.01f,1);scene.camera.fog_start=1;scene.camera.fog_end=7;scene.camera.fog_color=0xff183448;
  MaterialPass p;p.program=MaterialProgram::instances;p.shader.words[0]=0x342;p.shader.words[9]=0x10;p.shader.words[28]=0xffa08060;
  for(unsigned i=0;i<4;++i)p.shader.words[24+i]=f(1);
  scene.materials[0].passes={p};scene.lights={{0,{-1,1,-1},{.2f,.1f,-1},0,0xffe0d0c0,1,10}};return scene;
}
void compare(FrameRenderer& renderer,SceneFrame scene) {
  const auto actual=render(renderer,scene);auto& draw=scene.instances[0];draw.mesh=expanded(*draw.mesh,*draw.particle_matrices);draw.particle_matrices.reset();
  for(auto& p:scene.materials[0].passes)p.program=MaterialProgram::static_mesh;
  const auto expected=render(renderer,scene);unsigned maximum=0,visible=0;
  for(unsigned i=0;i<actual.size();++i){maximum=std::max(maximum,unsigned(std::abs(int(actual[i])-expected[i])));if(i%4!=3)visible+=actual[i]!=0;}
  check(visible>100,"invisible instance fixture");
  if(maximum>2)throw std::runtime_error("GPU instances differ from archived CPU expansion: "+std::to_string(maximum));
}
void cases() {
  FrameFixture fixture;const auto texture=fixture.add(0x26,{5,5,1});
  FrameRenderer renderer(fixture.p,fixture.g);const auto mesh=prototype();
  DemoFrame pattern;pattern.width=pattern.height=32;pattern.surfaces[0]={32,32};pattern.surfaces[13+texture]={32,32};
  for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
    FrameQuad q;q.rectangle={x/8.f,y/8.f,(x+1)/8.f,(y+1)/8.f};q.shader.words[0]=0x20002;q.shader.words[9]=0x10;q.shader.words[28]=0xff000000|(x*31<<16)|(y*31<<8)|((x*13+y*7)&255);
    pattern.commands.push_back({0,{13+texture,32,32},q});
  }
  renderer.render(pattern);
  auto matrices=std::make_shared<std::vector<SceneMatrix>>(std::initializer_list<SceneMatrix>{srt(-1,.4f,3,.05f,.8f),srt(.7f,-.3f,2.5f,-.07f,1.2f),srt(.2f,.7f,4,.13f,-.7f)});
  unsigned cases=0;
  for(unsigned mode:{0u,1u,2u,3u,0x10u,0x20u,0x30u,0x100u,0x200u,0x300u}) {
    auto scene=setup(mesh,matrices);auto& p=scene.materials[0].passes[0];auto& w=p.shader.words;
    if(mode>=0x10){const auto slot=mode&0x30 ? 2u : 3u;w[1]=mode;p.shader.textures[slot]=texture;w[4+slot]=0x101;w[9]=0;w[8+(slot==2 ? 9 : 10)]=0x10;}
    else if(mode==1){w[9]=0;w[19]=0x10;} // vertex color
    else if(mode>=2){p.usage=MaterialUsage::light;w[2]=5;w[1]=mode==2 ? 0x40 : 0;w[32]=f(8);}
    compare(renderer,scene);++cases;
  }
  // No matrix array and an empty array both draw zero instanced vertices.
  auto scene=setup(mesh,{});const auto empty=render(renderer,scene);scene.instances.clear();check(render(renderer,scene)==empty,"absent instance array draws prototype");
  scene=setup(mesh,std::make_shared<std::vector<SceneMatrix>>());check(render(renderer,scene)==empty,"empty instance array draws prototype");
  // Every normal/static draw following instancing must have per-vertex divisors.
  scene=setup(mesh,matrices);scene.materials[0].passes[0].program=MaterialProgram::static_mesh;scene.instances[0].world[14]=3;
  const auto baseline=render(renderer,scene);render(renderer,setup(mesh,matrices));check(render(renderer,scene)==baseline,"instance attributes leak into ordinary draws");
  scene.instances[0].particle_matrices.reset();check(render(renderer,scene)==baseline,"static pass should ignore instance array");
  // Per-mesh GPU matrix storage is reused as counts shrink, vanish and grow.
  scene=setup(mesh,matrices);render(renderer,scene);const auto before=renderer.stats();
  for(unsigned n:{1u,0u,3u,2u,3u}) {auto next=std::make_shared<std::vector<SceneMatrix>>(matrices->begin(),matrices->begin()+n);if(n)(*next)[0][12]+=.2f;scene.instances[0].particle_matrices=next;render(renderer,scene);}
  const auto after=renderer.stats();check(after.instance_uploads==before.instance_uploads && after.mesh_uploads==before.mesh_uploads,"instance buffers grow without reuse");check(after.instance_updates==before.instance_updates+4,"changed instance arrays were not uploaded");
  // Prototype outside the view/light still yields a visible translated copy.
  auto outside=prototype();for(auto& v:outside->vertices)v.position[0]+=100;
  auto core=std::make_shared<legacy::minimal::GenMinMesh>(*outside->min_kernel);for(int i=0;i<core->Vertices.Count;++i)core->Vertices[i].Pos.x+=100;outside->min_kernel=core;
  auto shifted=std::make_shared<std::vector<SceneMatrix>>(1,scene_identity());(*shifted)[0][12]=-100;(*shifted)[0][14]=3;
  auto moved=setup(outside,shifted);moved.materials[0].passes[0].usage=MaterialUsage::light;moved.materials[0].passes[0].shader.words[2]=5;moved.materials[0].passes[0].shader.words[1]=0x40;moved.materials[0].passes[0].shader.words[32]=f(8);compare(renderer,moved);
  std::cout<<cases+1<<" GPU instance cases match archived CPU expansion, including lighting, fog, normals, parent space and culling; zero counts and buffer reuse pass\n";
}
void limits() {
  auto mesh=prototype();const auto batch=scene_mesh_batches(*mesh)[0];check(scene_instance_limit(*mesh,batch)==65536,"prototype instance vertex cap");
  // A large vertex set reaches the archived 12 MiB buffer limit after four
  // copies; only the fifth transform would put the mesh in view.
  auto large=std::make_shared<Mesh>();const MeshVertex a{{-.2f,.2f,3}},b{{.2f,.2f,3}},c{{0,-.2f,3}};
  for(unsigned i=0;i<20000;++i){const auto base=unsigned(large->vertices.size());large->vertices.insert(large->vertices.end(),{a,b,c});MeshFace face;face.count=3;face.vertices={base,base+1,base+2,0};face.material=0;large->faces.push_back(face);}
  const auto batches=scene_mesh_batches(*large);check(scene_instance_limit(*large,batches[0])==4,"archived vertex cap");
  FrameFixture fixture;FrameRenderer renderer(fixture.p,fixture.g);auto poses=std::make_shared<std::vector<SceneMatrix>>(5,scene_identity());for(unsigned i=0;i<4;++i)(*poses)[i][12]=100;
  auto scene=setup(large,poses);const auto actual=render(renderer,scene);scene.instances.clear();check(render(renderer,scene)==actual,"renderer ignores archived instance cap");
  Mesh indexed=*mesh;indexed.faces.resize(100000,mesh->faces[0]);check(scene_instance_limit(indexed,scene_mesh_batches(indexed)[0])==3,"archived index cap uses two-byte divisor");
  auto clustered=*mesh;clustered.faces.push_back(clustered.faces[0]);clustered.faces[1].cluster=2;check(scene_mesh_batches(clustered).size()==2,"equal material clusters must retain separate job caps");
  std::cout<<"Archived per-cluster vertex/index instance caps match, including GPU clipping of excess copies\n";
}
}
int main(){try{cases();limits();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
