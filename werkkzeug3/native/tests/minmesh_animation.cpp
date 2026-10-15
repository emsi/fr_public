// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "minmesh_reference.hpp"
#include "wz3/texture.hpp"
#include "wz3/scene_draw.hpp"
#include "wz3/shadow.hpp"
#include <iostream>
namespace {
using namespace wz3;
namespace mm=legacy::minimal;
std::vector<std::uint32_t> parameters(unsigned mode=0) {
  std::vector<std::uint32_t> p(21);p[3]=f(.1f);p[15]=mode;p[16]=f(10);return p;
}
std::shared_ptr<mm::GenMinMesh> cube() {
  legacy::sFSRT srt{};srt.s.Init(1,1,1);srt.v[0]=srt.v[1]=srt.v[2]=1;return std::shared_ptr<mm::GenMinMesh>(mm::MinMesh_Cube(1,1,1,0,srt));
}
std::shared_ptr<const Mesh> exploded(std::span<const std::uint32_t> p,const Texture* bitmap=nullptr) {
  auto input=cube();return std::make_shared<Mesh>(mm::export_mesh(std::shared_ptr<mm::GenMinMesh>(mm::explode(*input,bitmap,p))));
}
void vector_near(Vec3 a,Vec3 b,const char* message,float tolerance=2e-5f) {
  for(unsigned i=0;i<3;++i)check(std::abs(a[i]-b[i])<=tolerance,message);
}
Vec3 vec(const mm::GenMinVector& v){return {v.x,v.y,v.z};}
template<class F> void rejects(F action){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid animation accepted");}
void topology_and_motion() {
  auto p=parameters();p[4]=f(2);p[5]=f(3);p[6]=f(-4);p[14]=f(.5f);
  const auto mesh=exploded(p);const auto& core=*mesh->min_kernel;
  check(core.Vertices.Count==144 && core.Faces.Count==48 && core.Animation->Matrices.Count==6,"Explode solid topology");
  check(core.CompletelyRigid,"Explode rigid binding");
  for(int i=0;i<core.Vertices.Count;++i){const auto& v=core.Vertices[i];check(v.BoneCount==1 && v.Matrix[0]==i/24 && v.Weights[0]==1,"Explode piece binding");}
  for(int i=0;i<core.Faces.Count;++i)for(unsigned c=0;c<core.Faces[i].Count;++c)check(core.Faces[i].Adjacent[c]>=0,"Explode piece is not closed");
  const auto original=cube();original->CalcNormals();
  const std::vector<std::uint8_t> mask(core.Faces.Count,1);auto shadow=prepare_shadow_mesh(*pose_mesh(mesh,0),mask);
  SceneDrawCache draw_cache;const auto* batches=&draw_cache.batches(mesh);MeshPoseCache pose_cache;std::weak_ptr<const Mesh> previous;

  for(float time:{-1.f,0.f,.49f,.5f,.75f,1.f,2.f}) {
    if(auto retained=previous.lock())check(retained.use_count()==2,"draw cache retained a transient pose");
    const auto pose=pose_cache.pose(mesh,time);previous=pose;check(pose->animation_source==mesh,"posed mesh lost cache identity");
    check(&draw_cache.batches(pose)==batches,"pose rebuilt immutable draw batches");
    SceneFrame scene;scene.instances={{0,pose}};const auto fresh_jobs=scene_draw_jobs(scene,128,96),cached_jobs=scene_draw_jobs(scene,128,96,&draw_cache);
    check(fresh_jobs.jobs.size()==cached_jobs.jobs.size(),"cached pose changed culling/job count");
    for(std::size_t i=0;i<fresh_jobs.jobs.size();++i){const auto& a=fresh_jobs.jobs[i];const auto& b=cached_jobs.jobs[i];check(a.sort_key==b.sort_key && a.batch==b.batch && a.material_pass==b.material_pass && a.shadow_zfail==b.shadow_zfail,"cached pose changed draw order");}
    update_shadow_mesh(shadow,*pose);const auto rebuilt=prepare_shadow_mesh(*pose,mask);
    check(shadow.vertices==rebuilt.vertices && shadow.planes==rebuilt.planes && shadow.triangles==rebuilt.triangles && shadow.triangle_planes==rebuilt.triangle_planes && shadow.edges.size()==rebuilt.edges.size(),"shadow pose update differs from full preparation");
    for(std::size_t i=0;i<shadow.edges.size();++i)check(shadow.edges[i].vertices==rebuilt.edges[i].vertices && shadow.edges[i].planes==rebuilt.edges[i].planes,"cached shadow changed welded edges");
    for(int piece=0;piece<6;++piece) {
      const auto n=vec(original->Faces[piece].Normal);const float t=std::max(0.f,time-.5f);
      Vec3 displacement{n[0]*3.5f*t,n[1]*3.5f*t-4*t*t,n[2]*3.5f*t};
      for(int i=piece*24;i<(piece+1)*24;++i) {
        auto expected=mesh->vertices[i].position;for(unsigned c=0;c<3;++c)expected[c]+=displacement[c];
        vector_near(pose->vertices[i].position,expected,"ballistic piece position");
        vector_near(vec(pose->min_kernel->Vertices[i].Normal),vec(core.Vertices[i].Normal),"translation changed normals");
        check(pose->vertices[i].point==mesh->vertices[i].point && pose->vertices[i].uv==mesh->vertices[i].uv,"pose changed topology/UVs");
      }
    }
    check_minmesh_shadow_reference(*pose,std::vector<std::uint8_t>(core.Faces.Count,1));
  }
  const auto again=pose_mesh(pose_mesh(mesh,2),0);for(unsigned i=0;i<mesh->vertices.size();++i)vector_near(again->vertices[i].position,mesh->vertices[i].position,"posing accumulated previous motion");
  check(pose_mesh(std::make_shared<Mesh>(),1)->vertices.empty(),"static mesh pose");
  rejects([&]{pose_mesh(mesh,NAN);});
  auto invalid=parameters(1);invalid[16]=0;rejects([&]{exploded(invalid);});
  mm::GenMinMesh empty;std::unique_ptr<mm::GenMinMesh> none(mm::explode(empty,nullptr,p));check(!none->Animation && !none->Faces.Count,"empty Explode behavior");
  auto too_many=cube();too_many->Faces.Resize(65536);for(int i=6;i<65536;++i)too_many->Faces[i]=too_many->Faces[0];
  std::unique_ptr<mm::GenMinMesh> limited(mm::explode(*too_many,nullptr,p));check(!limited->Faces.Count && !limited->Animation,"Explode 16-bit piece limit");
}
void timing_modes() {
  Texture bitmap;bitmap.width=4;bitmap.height=2;bitmap.pixels.resize(8);
  for(unsigned x=0;x<4;++x){bitmap.pixels[x][0]=32767;bitmap.pixels[4+x][0]=8191;}
  auto p=parameters();p[11]=f(2);p[12]=f(1);
  // Both bitmap axes intentionally use XSize in the archived handler. Cube
  // face-centre UV (0.5,0.5) therefore wraps to row zero in this 4x2 bitmap.
  auto mesh=exploded(p,&bitmap);for(const auto& m:mesh->min_kernel->Animation->Matrices.storage)near(m.Offset,0,"Explode bitmap V scale must use width");
  for(auto& pixel:bitmap.pixels)pixel[0]=0;
  mesh=exploded(p,&bitmap);for(const auto& m:mesh->min_kernel->Animation->Matrices.storage)near(m.Offset,-9999,"zero blue must delay piece indefinitely");
  mesh=exploded(p);for(const auto& m:mesh->min_kernel->Animation->Matrices.storage)near(m.Offset,-.5f,"radial delay uses squared distance");
  p=parameters(1);p[12]=f(0);
  mesh=exploded(p);near(mesh->min_kernel->Animation->Matrices[1].Offset,.05f,"directional wave offset");near(mesh->min_kernel->Animation->Matrices[3].Offset,-.05f,"directional wave offset");
}
void polygons() {
  for(unsigned count:{3u,4u,5u,8u})for(unsigned mode:{0u,2u}) {
    mm::GenMinMesh source;source.Vertices.Resize(count);source.Faces.Resize(2);
    auto& face=source.Faces[0];face.Count=count;face.Cluster=1;
    for(unsigned i=0;i<count;++i) {
      const auto angle=i*legacy::sPI2F/count;auto& v=source.Vertices[i];v.Pos.Init(std::cos(angle),std::sin(angle),i==1 ? .7f : 0.f);
      if(count==4){const Vec3 quad[]={{0,0,0},{1,0,1},{1,2,0},{0,1,1}};v.Pos.Init(quad[i][0],quad[i][1],quad[i][2]);}
      v.UV[0][0]=i*.1f;v.UV[1][1]=i*.2f;v.Color=0xff00ff00+i;face.Vertices[i]=i;
    }
    source.Faces[1]=face;source.Faces[1].Cluster=0;source.CalcNormals();
    auto p=parameters(mode);p[19]=f(.3f);const auto mesh=std::shared_ptr<mm::GenMinMesh>(mm::explode(source,nullptr,p));
    check(mesh->Vertices.Count==int(count*6) && mesh->Faces.Count==int(2*(count-2)+count) && mesh->Animation->Matrices.Count==1,"polygon degree/deleted-face handling");
    Vec3 center{};for(unsigned i=0;i<count;++i)for(unsigned c=0;c<3;++c)center[c]+=vec(source.Vertices[i].Pos)[c]/count;
    unsigned first=0;
    if(count==4 && mode) {
      legacy::sVector area[4];
      for(unsigned i=0;i<4;++i){legacy::sVector a,b;const auto p0=vec(source.Vertices[i].Pos),p1=vec(source.Vertices[(i+1)%4].Pos),p2=vec(source.Vertices[(i+2)%4].Pos);a.Init(p0[0]-p1[0],p0[1]-p1[1],p0[2]-p1[2]);b.Init(p1[0]-p2[0],p1[1]-p2[1],p1[2]-p2[2]);area[(i+1)%4].Cross3(a,b);}
      if(area[0].Dot3(area[2])>area[1].Dot3(area[3]))first=1;
      check(first==1,"quad fixture must select the alternate diagonal");
    }
    for(unsigned i=0;i<count;++i) {
      const auto& original=source.Vertices[(i+first)%count];const auto& top=mesh->Vertices[i];const auto& bottom=mesh->Vertices[i+count];
      vector_near(vec(top.Pos),vec(original.Pos),"polygon triangulation corner order");
      auto expected=vec(original.Pos);for(unsigned c=0;c<3;++c)expected[c]=center[c]+(expected[c]-center[c])*.7f-vec(original.Normal)[c]*.1f;
      vector_near(vec(bottom.Pos),expected,"extrusion depth/flatness");check(top.Color==original.Color && top.UV[1][1]==original.UV[1][1],"fragment attributes lost");
    }
  }
}
void random_rotations() {
  // Independent LCG stream from _types.cpp. Seed 1 is mixed to 146, then
  // warmed four times, with XOR before the last three calls.
  auto p=parameters();p[7]=f(.2f);p[8]=f(.3f);p[9]=f(.5f);p[10]=f(.7f);p[13]=f(.4f);p[20]=f(.6f);
  for(unsigned mode:{0u,4u}) {
    p[15]=mode;const auto mesh=exploded(p),pose=pose_mesh(mesh,1.f);std::uint32_t state=146;
    const auto next=[&](){const auto a=state*214013u+2531011u;state=a*214013u+2531011u;return ((state>>10)&65535u)|((a<<6)&0xffff0000u);};
    for(unsigned i=0;i<4;++i){if(i)state^=146u;next();}
    const auto rnd=[&](){return float(next()&0x3fffffffu)/1073741824.f;};
    const auto input=cube();
    for(unsigned piece=0;piece<6;++piece) {
      Vec3 speed;for(auto& c:speed)c=(rnd()*2-1)*.2f;const float delay=rnd()*.4f,t=1-delay;
      Vec3 rotation;rotation[2]=rnd()*2-1;rotation[1]=rnd()*2-1;rotation[0]=rnd()*2-1;
      legacy::sMatrix matrix;
      if(!mode)matrix.InitEuler(rotation[0]*.3f*legacy::sPI2F*t,rotation[1]*.5f*legacy::sPI2F*t,rotation[2]*.7f*legacy::sPI2F*t);
      else {legacy::sVector axis{rotation[0],rotation[1],rotation[2],0};axis.Unit3();matrix.InitRot(axis,(rnd()*2-1)*.6f*t*legacy::sPI2F);}
      near(mesh->min_kernel->Animation->Matrices[piece].Offset,-delay,"seeded piece delay");
      Vec3 center{};const auto& face=input->Faces[piece];for(unsigned c=0;c<4;++c)for(unsigned j=0;j<3;++j)center[j]+=vec(input->Vertices[face.Vertices[c]].Pos)[j]*.25f;
      for(unsigned i=piece*24;i<(piece+1)*24;++i) {
        const auto pos=mesh->vertices[i].position;legacy::sVector v{pos[0]-center[0],pos[1]-center[1],pos[2]-center[2],0};v.Rotate3(matrix);
        vector_near(pose->vertices[i].position,{v.x+center[0]+speed[0]*t,v.y+center[1]+speed[1]*t,v.z+center[2]+speed[2]*t},"seeded kinetic rotation");
        const auto n=mesh->min_kernel->Vertices[i].Normal;v.Init(n.x,n.y,n.z,0);v.Rotate3(matrix);vector_near(vec(pose->min_kernel->Vertices[i].Normal),{v.x,v.y,v.z},"rotated normals");
        v.Init(mesh->vertices[i].tangent[0],mesh->vertices[i].tangent[1],mesh->vertices[i].tangent[2],0);v.Rotate3(matrix);vector_near(pose->vertices[i].tangent,{v.x,v.y,v.z},"rotated tangents");
      }
    }
  }
}
void recipes(const char* path,const char* font) {
  TextureOptions options;options.font_file=font;
  const auto p=read_project(path);const auto g=connect(p);unsigned recipes=0,complete=0;
  for(unsigned mode=0;mode<8;++mode)for(float flat:{0.f,.2f,-.1f}) {
    auto params=parameters(mode);params[19]=f(flat);const auto mesh=exploded(params);for(float t:{0.f,.1f,1.f,3.f})validate_mesh(*pose_mesh(mesh,t));
  }
  for(std::size_t i=0;i<p.operators.size();++i)if(g.nodes[i].reachable && p.operators[i].command==0x134) {
    const auto mesh=exploded(p.operators[i].parameters);for(float t:{0.f,.2f,1.f,10.f})validate_mesh(*pose_mesh(mesh,t));++recipes;
    const auto real=evaluate_mesh(p,g,i,options);for(float t:{0.f,.2f,1.f})validate_mesh(*pose_mesh(real,t));++complete;
  }
  const auto first=evaluate_mesh(p,g,8157);check(mesh_has_animation(*first),"material link discarded Explode animation");
  check(recipes==46 && complete==46,"Debris Explode coverage missing");
  std::cout<<recipes<<" saved Explode recipes and "<<complete<<" complete graphs pose; analytic motion, RNG, bitmap delays and archived shadow planes pass\n";
}
}
int main(int argc,char** argv){try{topology_and_motion();timing_modes();polygons();random_rotations();if(argc>2)recipes(argv[1],argv[2]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
