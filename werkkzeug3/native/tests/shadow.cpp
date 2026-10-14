// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/shadow.hpp"
#include "wz3/scene_draw.hpp"
#include "wz3/lighting.hpp"
#include "mesh_kernel.hpp"
#include <iostream>
#include <set>
namespace source {
using namespace wz3::legacy;
struct GenMesh: wz3::legacy::GenMesh {
  explicit GenMesh(const wz3::legacy::GenMesh& m):wz3::legacy::GenMesh(m){}
  void CalcFacePlane(sVector&,sInt);
};
enum {MPP_SHADOW,MPP_SPRITES,MPP_STATIC,MPP_INSTANCES,MPP_INSTANCES_SH};
struct SilEdge {sInt Vert[2],Face[2];};
struct EngPaintInfo {sVector LightPos;sF32 LightRange{};};
struct SVCache {sInt* IndexBuffer{};sInt SilIndices{},CapIndices{};~SVCache(){delete[] IndexBuffer;}};
struct EngMesh {
  struct Ints:std::vector<sInt> {operator sInt*(){return data();}};
  struct Job {
    sInt Program=MPP_SHADOW,MtrlId=1,VertexCount{},IndexCount{},EdgeCount{},PlaneCount{};
    sAABox BBox;sVector* Planes{};Ints VertexBuffer,IndexBuffer,FaceMap,FacePartEnd,EdgePartEnd;SilEdge* Edges{};
    void Alloc(sInt v,sInt i,sInt e,sInt f,sInt p){VertexCount=v;IndexCount=i;EdgeCount=e;VertexBuffer.resize(v);IndexBuffer.resize(i);FaceMap.resize(f);FacePartEnd.resize(p);EdgePartEnd.resize(p);Edges=new SilEdge[e];}
    ~Job(){delete[] Planes;delete[] Edges;}
  };
  sInt PartCount{};sInt SFaceIn[1<<20]{},SPartSkip[1]{};
  void CalcBoundingBox(GenMesh*,sInt,sAABox&);
  void PrepareJob(Job*,GenMesh*,sBool&,const sBool*);
  void UpdateShadowCacheJob(Job*,sVector*,SVCache*,const EngPaintInfo&);
  void PrepareSpriteJob(Job*,GenMesh*){throw std::runtime_error("oracle only hosts shadows");}
};
struct sFRect {float x0,y0,x1,y1;};
struct sCullPlane {
  sVector Plane;
  void From3Points(const sVector& a,const sVector& b,const sVector& c){sVector u,v;u.Sub3(b,a);v.Sub3(c,a);Plane.Cross3(u,v);Plane.w=-Plane.Dot3(a);}
  float Distance(const sVector& v){return Plane.Dot3(v)+Plane.w;}
  void CalcNVert(){} // Only the generated planes are compared here.
};
struct sFrustum {sCullPlane Planes[6];sInt NPlanes{};void ZFailVolume(const sVector&,const sMatrix&,sF32,sF32[2],const sFRect&);};
#define sZONE(x)
#define sVERIFY(x) check(bool(x),"archived shadow assertion")
#define SHADOWS 1
#define THICKLINES 0
#define sINTRO 1
#include "shadow_oracle.inc"
#undef sINTRO
#undef THICKLINES
#undef SHADOWS
#undef sVERIFY
#undef sZONE
}
namespace {
void oracle(const wz3::Mesh& mesh,const std::vector<std::uint8_t>& mask) {
  check(bool(mesh.kernel),"oracle requires indexed mesh");source::GenMesh core(*mesh.kernel);
  auto has=std::make_unique<bool[]>(mask.size()+1);has[0]=false;
  for(std::size_t f=0;f<mask.size();++f){has[f+1]=mask[f];if(core.Face[f].Material)core.Face[f].Material=f+1;}
  auto engine=std::make_unique<source::EngMesh>();source::EngMesh::Job expected;bool first=true;engine->PrepareJob(&expected,&core,first,has.get());
  const auto actual=wz3::prepare_shadow_mesh(mesh,mask);check(actual.planes.size()==std::size_t(expected.PlaneCount),"shadow plane count");
  for(std::size_t i=0;i<actual.planes.size();++i)for(unsigned c=0;c<4;++c)near(actual.planes[i][c],expected.Planes[i][c],"shadow plane differs from archived preparation");
  check(actual.edges.size()==std::size_t(expected.EdgeCount) && actual.triangles.size()==std::size_t(expected.IndexCount),"shadow edge/cap count");
  const auto mapped=[&](unsigned i){return unsigned(expected.VertexBuffer.at(i/2)*2)+(i&1);};
  for(std::size_t i=0;i<actual.edges.size();++i)for(unsigned c=0;c<2;++c)check(actual.edges[i].vertices[c]==mapped(expected.Edges[i].Vert[c]) && actual.edges[i].planes[c]==unsigned(expected.Edges[i].Face[c]),"shadow edge differs from archived preparation");
  for(std::size_t i=0;i<actual.triangles.size();++i)check(actual.triangles[i]==mapped(expected.IndexBuffer[i]),"shadow cap triangulation differs from archive");
  for(const wz3::Vec3 light:std::array<wz3::Vec3,5>{{{0,0,0},{10,20,30},{-10,-20,-30},{.001f,100,0},{100,-.001f,20}}}) {
    source::SVCache cache;source::EngPaintInfo info;info.LightPos.Init(light[0],light[1],light[2]);engine->UpdateShadowCacheJob(&expected,expected.Planes,&cache,info);
    const auto indices=wz3::shadow_indices(actual,light);check(indices.silhouette_count==std::size_t(cache.SilIndices) && indices.indices.size()==std::size_t(cache.SilIndices+cache.CapIndices),"shadow silhouette/cap sizes");
    for(std::size_t i=0;i<indices.indices.size();++i)check(indices.indices[i]==mapped(cache.IndexBuffer[i]),"shadow indices differ from archived silhouette/cap generator");
  }
}
void near_volume() {
  wz3::SceneCamera camera;const std::array<float,4> rect={-1,-1,1,1};
  const auto behind=wz3::shadow_near_volume(camera,{0,0,0},rect),ahead=wz3::shadow_near_volume(camera,{0,0,4},rect);
  check(behind.size()==6 && ahead.size()==6,"near-volume plane count");
  check(!wz3::shadow_box_culled({{-.01f,-.01f,.04f},{.01f,.01f,.08f}},behind),"caster between eye and near plane needs z-fail");
  check(wz3::shadow_box_culled({{-.1f,-.1f,1},{.1f,.1f,2}},behind),"caster beyond near plane can use z-pass for camera light");
  check(!wz3::shadow_box_culled({{-.1f,-.1f,1},{.1f,.1f,2}},ahead),"caster between near plane and light needs z-fail");
  check(wz3::shadow_box_culled({{5,5,1},{6,6,2}},ahead),"outside near-pyramid caster can use z-pass");
  check(wz3::shadow_near_volume(camera,{0,0,camera.near_clip},rect).size()==2,"light on near plane uses degenerate volume");
  for(unsigned i=0;i<20;++i) {
    source::sMatrix m;m.InitEuler(i*.03125f,i*-.125f,i*.25f);m.l.Init(i*.1f,-float(i)*.2f,i*.3f);
    camera.world={m.i.x,m.i.y,m.i.z,0,m.j.x,m.j.y,m.j.z,0,m.k.x,m.k.y,m.k.z,0,m.l.x,m.l.y,m.l.z,1};camera.near_clip=(i+1)*.125f;camera.zoom_x=.5f+i*.125f;camera.zoom_y=.3f+i*.0625f;
    const std::array<float,4> r={-.8f,-.3f,.4f,.9f};float zoom[]={camera.zoom_x,camera.zoom_y};
    for(unsigned k=0;k<3;++k){source::sVector l;l.Init(i*.3f,-float(i)*.5f,int(k)*5-5.f);source::sFrustum reference;reference.ZFailVolume(l,m,camera.near_clip,zoom,{r[0],r[1],r[2],r[3]});const auto actual=wz3::shadow_near_volume(camera,{l.x,l.y,l.z},r);check(actual.size()==std::size_t(reference.NPlanes),"near volume differs from archived plane count");for(std::size_t p=0;p<actual.size();++p)for(unsigned c=0;c<4;++c)near(actual[p][c],reference.Planes[p].Plane[c],"near volume differs from archive");}
  }
}
void shared_batches() {
  FrameFixture f;const auto cube=f.add(0x81,{1,1,1,1,::f(1),::f(1),::f(1),0,0,0,0,0,0});
  const auto source=wz3::evaluate_mesh(f.p,f.g,cube);auto mesh=std::make_shared<wz3::Mesh>(*source);mesh->kernel.reset();
  const auto n=std::uint32_t(mesh->vertices.size());for(auto& v:mesh->vertices){v.position[0]+=100;v.position[2]+=4;}
  for(auto& face:mesh->faces)face.material=0;
  for(auto v:source->vertices){v.position[2]+=4;if(v.point!=wz3::mesh_no_point)v.point+=n;mesh->vertices.push_back(v);}
  for(auto face:source->faces){for(unsigned c=0;c<face.count;++c)face.vertices[c]+=n;face.material=1;mesh->faces.push_back(face);}
  wz3::SceneFrame s;s.instances={{0,mesh}};s.lights={{0,{0,0,2},{},2,0xffffffff,1,6}};
  wz3::MaterialPass p;p.usage=wz3::MaterialUsage::shadow;p.program=wz3::MaterialProgram::shadow;p.render_pass=17;s.materials[0].passes={p};p.render_pass=29;s.materials[1].passes={p};
  const auto jobs=wz3::scene_draw_jobs(s,128,128);check(jobs.jobs.size()==1 && jobs.jobs[0].batch==0 && (jobs.jobs[0].sort_key>>24)==17,"shadow job must use first material and whole-mesh bounds");
  auto mask=wz3::shadow_face_mask(*mesh,s.materials);const auto both=wz3::prepare_shadow_mesh(*mesh,mask);check(both.triangles.size()==72,"shared shadow job must include both closed cubes");
  for(std::size_t i=0;i<source->faces.size();++i)mesh->faces[i].casts_shadow=false;
  mask=wz3::shadow_face_mask(*mesh,s.materials);check(wz3::prepare_shadow_mesh(*mesh,mask).triangles.size()==36,"ShadowEnable face flag ignored");
  s.materials[1].passes.clear();mask=wz3::shadow_face_mask(*mesh,s.materials);check(wz3::prepare_shadow_mesh(*mesh,mask).triangles.empty(),"material without shadow finalizer still casts");
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::FramePlayer player(p,g);std::set<const wz3::Mesh*> checked;unsigned volumes=0,zpass=0,zfail=0;
  for(int beat:{56,72,96,112,144,160,184,216,240,272,304,368,416,448}) {
    const auto frame=player.frame(g.names.at("root"),beat*65536,320,240);
    for(const auto& command:frame.commands)if(const auto* scene=std::get_if<wz3::SceneFrame>(&command.draw)) {
      const auto draws=wz3::scene_draw_jobs(*scene,command.target.width,command.target.height);
      for(const auto& job:draws.jobs)if(!job.effect && ((job.sort_key>>16)&15)==3) {
        zfail+=job.shadow_zfail;zpass+=!job.shadow_zfail;const auto& mesh=*scene->instances[job.instance].mesh;
        if(checked.insert(&mesh).second){oracle(mesh,wz3::shadow_face_mask(mesh,scene->materials));++volumes;}
      }
    }
  }
  check(volumes>10 && zpass>0 && zfail>0,"Theta shadow coverage missing");std::cout<<volumes<<" distinct Theta shadow meshes match archived preparation and indices; "<<zpass<<" z-pass / "<<zfail<<" z-fail jobs\n";
}
}
int main(int argc,char** argv){try{near_volume();shared_batches();if(argc>1)theta(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
