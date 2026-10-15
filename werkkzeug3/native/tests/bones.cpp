// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "bone_fixture.hpp"
#include "minmesh_reference.hpp"
#include <cfenv>
#include <iostream>
#include <limits>
namespace {
using namespace wz3;namespace mm=legacy::minimal;
template<class F> void rejects(F action){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid bone input accepted");}
void compare(const std::shared_ptr<const Mesh>& mesh,float time,bool shadows=false,MeshPoseCache* cache=nullptr) {
  const auto actual=cache ? cache->pose(mesh,time) : pose_mesh(mesh,time),expected=bone_reference_pose(mesh,time);
  check(actual->animation_source==mesh,"weighted pose lost static cache identity");
  for(unsigned i=0;i<actual->vertices.size();++i) {
    const auto& a=actual->min_kernel->Vertices[i];const auto& b=expected->min_kernel->Vertices[i];
    for(auto pair:{std::pair{a.Pos,b.Pos},std::pair{a.Normal,b.Normal},std::pair{a.Tangent,b.Tangent}}) {
#if WZ3_BONE_SSE_ORACLE
      check(pair.first.x==pair.second.x && pair.first.y==pair.second.y && pair.first.z==pair.second.z,"weighted pose differs from archived SSE instructions");
#else
      near(pair.first.x,pair.second.x,"weighted X");near(pair.first.y,pair.second.y,"weighted Y");near(pair.first.z,pair.second.z,"weighted Z");
#endif
    }
    check(actual->vertices[i].position==Vec3{a.Pos.x,a.Pos.y,a.Pos.z},"posed export position");
    check(actual->vertices[i].uv==mesh->vertices[i].uv && actual->vertices[i].point==mesh->vertices[i].point && actual->vertices[i].color==mesh->vertices[i].color,"weighted pose changed UV/color/topology");
  }
  if(shadows) {
    std::vector<std::uint8_t> mask;
    for(int i=0;i<actual->min_kernel->Faces.Count;++i){const auto& face=actual->min_kernel->Faces[i];mask.push_back(face.Cluster && face.Count>=3 && !(face.Flags&1));}
    check_minmesh_shadow_reference(*actual,mask);
  }
}
void binding() {
  mm::GenMinMesh mesh;mesh.Vertices.Resize(4);
  mesh.Vertices[0].Pos.Init(0,0,-1);mesh.Vertices[1].Pos.Init(0,0,.5f);mesh.Vertices[2].Pos.Init(0,0,1);mesh.Vertices[3].Pos.Init(0,0,2);
  std::array<std::uint32_t,8> p{0,0,0,0,0,f(1),6,0};mm::bone_chain(mesh,p);
  check(mesh.Animation->Matrices.Count==6 && mesh.Clusters[1].AnimType==2,"BoneChain animation/cluster binding");
  const auto& mid=mesh.Vertices[1];const std::array<float,4> weights{-.0625f,.5625f,.5625f,-.0625f};
  for(unsigned i=0;i<4;++i){check(mid.Matrix[i]==i+1 && mid.Weights[i]==weights[i],"cubic midpoint weights");}
  check(mesh.Vertices[0].Matrix[1]==0 && mesh.Vertices[0].Weights[1]==1,"lower chain endpoint clamp");
  for(unsigned i:{2u,3u})check(mesh.Vertices[i].BoneCount==1 && mesh.Vertices[i].Matrix[0]==5 && mesh.Vertices[i].Weights[0]==1,"upper chain endpoint clamp");
  for(int i=0;i<6;++i){near(mesh.Animation->Matrices[i].BasePose.l.z,-i/5.f,"chain inverse bind pose");near(mesh.Animation->Matrices[i].NoAnimation.l.z,i/5.f,"chain rest pose");}
  // Keep the archived Pos.x in the Y difference; this is not a corrected axis projection.
  mesh.Vertices[0].Pos.Init(.3f,-100,.2f);mesh.Vertices[1].Pos.Init(.3f,100,.2f);p[3]=p[4]=f(1);mm::bone_chain(mesh,p);
  for(unsigned i=0;i<4;++i)check(mesh.Vertices[0].Weights[i]==mesh.Vertices[1].Weights[i] && mesh.Vertices[0].Matrix[i]==mesh.Vertices[1].Matrix[i],"BoneChain changed archived Y-coordinate behavior");
  for(unsigned bad:{1u,256u}){auto invalid=p;invalid[6]=bad;rejects([&]{mm::bone_chain(mesh,invalid);});}
  p[3]=p[4]=p[5]=0;rejects([&]{mm::bone_chain(mesh,p);});
  mm::GenMinMesh empty;p[7]=1;rejects([&]{mm::bone_chain(empty,p);});
}
void modes() {
  BoneFixture x;const auto curve=x.curve();auto spline=mm::bone_spline(x.p,x.g,curve);
  for(unsigned mode=0;mode<8;++mode) {
    mm::GenMinMesh mesh;mesh.CreateAnimation(7);for(int i=0;i<7;++i){auto& m=mesh.Animation->Matrices[i];m.Offset=.1f;m.Factor=.2f;m.Spread=.3f;}
    mm::bone_train(mesh,spline,.6f,mode,-.25f);
    for(int i=0;i<7;++i){const auto& m=mesh.Animation->Matrices[i];const auto time=i*.6f/6-.25f;check(m.Spline==spline,"BoneTrain lost shared curve");
      if((mode&3)==0){near(m.Offset,time,"shift offset");check(m.Factor==1 && m.Spread==0,"shift factors");}
      if((mode&3)==1){near(m.Factor,time,"scale factor");check(m.Offset==0 && m.Spread==0,"scale offsets");}
      if((mode&3)==2)check(m.Offset==0 && m.Factor==0 && m.Spread==1,"spread mode");
      if((mode&3)==3)check(m.Offset==.1f && m.Factor==.2f && m.Spread==.3f,"reserved mode overwrote timing");
    }
    auto copy=mm::GenMinMesh(mesh);mm::bone_train(copy,spline,0,0,.7f);check(mesh.Animation->Matrices[0].Offset==.7f,"BoneTrain lost original shared animation ownership");
  }
  mm::GenMinMesh one;one.CreateAnimation(1);rejects([&]{mm::bone_train(one,spline,1,0,0);});mm::bone_train(one,spline,1,2,0);
  legacy::sMatrix matrix;one.Animation->EvalAnimation(.5f,0,&matrix);check(std::isfinite(matrix.l.x),"single-bone spread");
  rejects([&]{mm::bone_train(one,spline,std::numeric_limits<float>::infinity(),2,0);});
  std::shared_ptr<mm::GenSpline> retained;{BoneFixture temporary;const auto id=temporary.curve();retained=mm::bone_spline(temporary.p,temporary.g,id);}
  float zoom;retained->Eval(.5f,0,matrix,zoom);check(std::isfinite(matrix.l.z),"bone curve did not survive graph evaluator");
}
void poses() {
  unsigned cases=0;
  for(unsigned mode=0;mode<8;++mode) {
    BoneFixture x;const auto id=x.train(mode);auto mesh=evaluate_mesh(x.p,x.g,id);MeshPoseCache cache;
    for(float time:{-1.f,0.f,.001f,.2f,.5f,.9f,1.f,1.7f}){compare(mesh,time,true,&cache);++cases;}
    const auto first=pose_mesh(mesh,.2f),again=pose_mesh(pose_mesh(mesh,.9f),.2f);
    for(unsigned i=0;i<mesh->vertices.size();++i)check(first->vertices[i].position==again->vertices[i].position,"weighted poses accumulated");
    std::fesetround(FE_DOWNWARD);compare(mesh,.2f,false,&cache);check(std::fegetround()==FE_DOWNWARD,"pose leaked rounding mode");std::fesetround(FE_TONEAREST);
  }
  // Probe sorted/pruned influence combinations and nonuniform matrices, with
  // deliberately different normals/tangents so position-only skinning fails.
  auto core=std::make_shared<mm::GenMinMesh>();core->Vertices.Resize(129);core->CreateAnimation(4);core->Clusters[1].AnimType=2;
  for(int b=0;b<4;++b){auto& m=core->Animation->Matrices[b].NoAnimation;m.InitEuler(.13f*b,.27f*b,.41f*b);m.i.Scale3(1+b*.2f);m.j.Scale3(.7f);m.k.Scale3(1.3f);m.l.Init(b*3.7f,-b*.37f,b*.9f);}
  for(int i=0;i<129;++i){auto& v=core->Vertices[i];v.Pos.Init(.13f*i-.8f,.31f*i-.2f,.07f*i-.1f);v.BoneCount=4;v.Weights[0]=i/128.f;v.Weights[1]=.75f-v.Weights[0];v.Weights[2]=-.015625f+i/8192.f;v.Weights[3]=.25f-v.Weights[2];for(unsigned b=0;b<4;++b)v.Matrix[b]=(b+i)%4;}
  auto mesh=std::make_shared<Mesh>(mm::export_mesh(core));for(auto& v:core->Vertices.storage){v.Normal.Init(.3f,.7f,-.2f);v.Tangent.Init(-.9f,.1f,.6f);}
  compare(mesh,.5f);check(core->Vertices[0].Normal.x==.3f,"pose changed source normals");
  std::cout<<cases<<" spline timing poses and 129 weighted vertices match archived skinning; dynamic shadow planes agree\n";
}
void retained_poses() {
  BoneFixture x;const auto id=x.train();const auto mesh=evaluate_mesh(x.p,x.g,id);MeshPoseCache cache;
  std::vector<std::shared_ptr<const Mesh>> retained;
  for(float time:{.1f,.2f,.3f,.4f,.5f})retained.push_back(cache.pose(mesh,time));
  for(unsigned i=0;i<retained.size();++i) {
    const auto expected=bone_reference_pose(mesh,(i+1)*.1f);
    for(unsigned v=0;v<mesh->vertices.size();++v)check(retained[i]->vertices[v].position==expected->vertices[v].position,"pose pool mutated a retained frame");
  }
  auto pose=cache.pose(mesh,.7f);auto core=pose->min_kernel;const auto position=core->Vertices[0].Pos;pose.reset();
  for(float time:{.8f,.9f,.2f})cache.pose(mesh,time);
  check(core->Vertices[0].Pos.x==position.x && core->Vertices[0].Pos.y==position.y && core->Vertices[0].Pos.z==position.z,"pose pool mutated a retained MINMESH core");
}
void debris(const char* path) {
  const auto p=read_project(path);const auto g=connect(p);unsigned chains=0,trains=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable) {
    const auto command=p.operators[id].command;if(command!=0x128 && command!=0x129)continue;
    std::cout<<"Checking Debris bone operator "<<id<<std::endl;
    const auto mesh=evaluate_mesh(p,g,id);check(mesh_has_animation(*mesh),"saved bone animation missing");
    if(command==0x128){++chains;compare(mesh,0);}
    else {++trains;for(float time:{0.f,.25f,.5f,1.f,1.5f})compare(mesh,time,time==.5f);}
  }
  check(chains==17 && trains==17,"missing saved BoneChain/BoneTrain cases");
  std::cout<<chains<<" saved BoneChains and "<<trains<<" complete BoneTrain graphs match archived skinning\n";
}
}
int main(int argc,char** argv){try{binding();modes();poses();retained_poses();if(argc>1)debris(argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
