// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "minmesh_font.hpp"
#include "minmesh_reference.hpp"
#include <cfenv>
#include <iostream>
#include <limits>
namespace {
using namespace wz3;namespace mm=legacy::minimal;
template<class F> void rejects(F action){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,"invalid Font3D accepted");}
mm::Font3DContour contour(std::initializer_list<Vec3> points) {
  mm::Font3DContour out;unsigned i=0;
  for(auto p:points){mm::GenMinVector v{p[0],p[1],p[2]};if(i++)out.segments.push_back({false,{},v});else out.start=v;}return out;
}
double volume(const Mesh& mesh) {
  double result=0;
  for(const auto& face:mesh.faces)for(unsigned i=2;i<face.count;++i){const auto a=mesh.vertices[face.vertices[0]].position,b=mesh.vertices[face.vertices[i-1]].position,c=mesh.vertices[face.vertices[i]].position;
    result+=(double(a[0])*(b[1]*c[2]-b[2]*c[1])+double(a[1])*(b[2]*c[0]-b[0]*c[2])+double(a[2])*(b[0]*c[1]-b[1]*c[0]))/6;}
  return result;
}
double area(const mm::GenMinMesh& mesh) {
  double result=0;for(int i=0;i<mesh.Faces.Count;++i){const auto& f=mesh.Faces[i];check(f.Count==3,"flat Font3D must be triangles");
    const auto a=mesh.Vertices[f.Vertices[0]].Pos,b=mesh.Vertices[f.Vertices[1]].Pos,c=mesh.Vertices[f.Vertices[2]].Pos;
    result+=((double(b.y)-a.y)*(c.x-a.x)-(double(b.x)-a.x)*(c.y-a.y))*.5;}
  return result;
}
void shadow(const Mesh& mesh,bool closed=true) {
  std::vector<std::uint8_t> mask;for(int i=0;i<mesh.min_kernel->Faces.Count;++i){const auto& face=mesh.min_kernel->Faces[i];mask.push_back(face.Cluster && face.Count>=3 && !(face.Flags&1));}
  check_minmesh_shadow_reference(mesh,mask);
  if(closed)for(int i=0;i<mesh.min_kernel->Faces.Count;++i){const auto& face=mesh.min_kernel->Faces[i];for(unsigned c=0;c<face.Count;++c)check(face.Adjacent[c]>=0,"extruded Font3D has open boundary");}
}
void analytic() {
  const mm::Font3DGlyph ring{contour({{0,0,0},{0,4,0},{4,4,0},{4,0,0},{0,0,0}}),contour({{1,1,0},{3,1,0},{3,3,0},{1,3,0},{1,1,0}})};
  const std::array paths{ring};const auto flat=mm::font3d_paths(paths,1,0,.05f);near(area(*flat),12,"triangulation filled the hole or reversed winding");
  for(float extrusion:{.25f,1.f,-.5f}) {
    auto core=mm::font3d_paths(paths,1,extrusion,.05f);const auto mesh=mm::export_mesh(core);near(volume(mesh),12*extrusion,"extrusion volume/winding");shadow(mesh);
    check(core->Vertices.Count==4*flat->Vertices.Count,"Font3D sharp-edge vertex copies");
    unsigned sides=0;for(int i=0;i<core->Faces.Count;++i){const auto& face=core->Faces[i];sides+=face.Count==4;check(face.Cluster==1 && face.Select==0 && face.Flags==0,"Font3D polygon attributes");}
    check(sides==8,"hole boundary extrusion count");
    for(int i=0;i<core->Vertices.Count;++i){const auto& v=core->Vertices[i];near(v.UV[0][0],v.Pos.x+v.Pos.z,"Font3D source UV shear");near(v.UV[0][1],v.Pos.y,"Font3D V coordinate");check(v.Select==0 && v.Color==0,"Font3D source vertex flags/color");}
    // GLU can leave contour endpoints unused. UnitSafe gives those an arbitrary
    // fallback normal; only vertices referenced by the front cap are relevant.
    for(int i=0;i<flat->Faces.Count;++i)for(unsigned j=0;j<core->Faces[i].Count;++j)
      near(core->Vertices[core->Faces[i].Vertices[j]].Normal.z,-1,"cap normal must stay sharp");
  }
  // Each glyph is tessellated separately: overlap must not cancel two letters.
  const std::array overlap{ring,ring};near(area(*mm::font3d_paths(overlap,1,0,.05f)),24,"glyph boundaries lost");
  const std::array bow{mm::Font3DGlyph{contour({{0,0,0},{2,2,0},{0,2,0},{2,0,0},{0,0,0}})}};
  const auto crossed=mm::font3d_paths(bow,1,0,.05f);near(area(*crossed),2,"GLU intersection triangulation");check(crossed->Vertices.Count>5,"combine callback did not generate intersection vertex");
  mm::Font3DContour curve;curve.start={0,0,0};curve.segments={{true,{0,2,0},{2,2,0}},{false,{}, {2,0,0}},{false,{}, {0,0,0}}};
  const std::array curved{mm::Font3DGlyph{curve}};const auto coarse=mm::font3d_paths(curved,1,0,1),fine=mm::font3d_paths(curved,1,0,.001f);
  check(fine->Vertices.Count>coarse->Vertices.Count,"curve tolerance has no effect");check(std::abs(area(*fine)-10./3)<.0002,"quadratic subdivision area");
  for(int i=0;i<fine->Vertices.Count;++i){const auto& p=fine->Vertices[i].Pos;
    // The GLU combine callback may append another copy of a closed endpoint.
    if(p.y==0)check(p.x==0 || p.x==2,"unexpected line endpoint");
    else {const auto t=std::sqrt(p.x/2);near(p.y,4*t-2*t*t,"de Casteljau subdivision point");}}
  const auto empty=mm::font3d_paths({},1,.1f,.05f);check(empty->Vertices.Count==0 && empty->Faces.Count==0,"empty Font3D");
  std::fesetround(FE_DOWNWARD);const auto rounded=mm::font3d_paths(paths,1,.25f,.05f);check(std::fegetround()==FE_DOWNWARD,"Font3D leaked rounding mode");std::fesetround(FE_TONEAREST);near(volume(mm::export_mesh(rounded)),3,"Font3D depends on caller rounding");
  rejects([&]{mm::font3d_paths(paths,1,INFINITY,.1f);});rejects([&]{mm::font3d_paths(paths,1,.1f,-1);});rejects([&]{mm::font3d_paths(paths,INFINITY,.1f,.1f);});
  std::cout<<"Font3D holes, intersecting contours, curve area, extrusion, UVs, sharp caps and shadows agree with analytic geometry\n";
}
void fonts(const char* path) {
  for(const auto text:{"B8O", "AV AV", "andromedasoftwaredevelopment.", " ", ""}) {
    const auto core=mm::font3d_mesh(path,text,1,.1f,.05f);const auto mesh=mm::export_mesh(core);
    if(text[0] && text[0]!=' '){check(!mesh.faces.empty(),"text mesh empty");check(volume(mesh)>0,"text extrusion winding");shadow(mesh);}
  }
  const auto small=mm::font3d_mesh(path,"O",0,.1f,.05f),minimum=mm::font3d_mesh(path,"O",8/128.f,.1f,.05f);check(small->Vertices.Count==minimum->Vertices.Count,"Font3D minimum height clamp");
  const auto plain=mm::font3d_outlines(path,"AA",1),spaced=mm::font3d_outlines(path,"A A",1);check(plain.size()==2 && spaced.size()==3 && spaced[1].empty(),"Font3D space outline");check(spaced[2][0].start.x>plain[1][0].start.x,"Font3D space did not advance pen");
  rejects([&]{mm::font3d_mesh({},"x",1,.1f,.05f);});
}
void debris(const char* project,const char* font) {
  const auto p=read_project(project);const auto g=connect(p);TextureOptions options;options.font_file=font;unsigned count=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0x133) {
    std::cout<<"Checking Font3D "<<id<<' '<<p.operators[id].strings[0]<<std::endl;const auto mesh=evaluate_mesh(p,g,id,options);shadow(*mesh);check(volume(*mesh)>0,"saved text volume");++count;
  }
  check(count==29,"missing saved Font3D recipes");std::cout<<count<<" saved Font3D recipes generate closed meshes with archived shadow topology\n";
}
}
int main(int argc,char** argv){try{analytic();if(argc>1)fonts(argv[1]);if(argc>2)debris(argv[2],argv[1]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
