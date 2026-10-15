// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_kernel.hpp"
#include "minmesh_font.hpp"
#include "wz3/texture.hpp"
#include "wz3/spline3d.hpp"
#include <bit>
#include <tuple>

namespace wz3::legacy::minimal {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void parameters(const Operator& op,unsigned count){require(op.parameters.size()==count,"unsupported MINMESH parameter layout");}
unsigned integer(const Operator& op,unsigned i,unsigned max=255,unsigned min=0) {
  const auto v=op.parameters.at(i);require(v>=min && v<=max,"invalid MINMESH integer parameter");return v;
}
float real(const Operator& op,unsigned i) {
  const auto v=as_float(op.parameters.at(i));require(std::isfinite(v) && std::abs(v)<=1000000,"invalid MINMESH float parameter");return v;
}
sVector vector(const Operator& op,unsigned i){return {real(op,i),real(op,i+1),real(op,i+2),0};}
sFSRT srt(const Operator& op,unsigned i) {
  sFSRT out{};out.s=vector(op,i);out.r=vector(op,i+3);out.t=vector(op,i+6);
  for(unsigned c=0;c<9;++c)out.v[c]=real(op,i+c);
  return out;
}
class Evaluator {
  const TextureOptions& options;
  const Project& project;const Graph& graph;
  std::vector<std::shared_ptr<const GenMinMesh>> cache;
  std::vector<bool> visiting;
  std::shared_ptr<MaterialArena> materials=std::make_shared<MaterialArena>();
  std::size_t allocated{};
  std::size_t concrete(std::size_t id)const {
    for(unsigned depth=0;depth<512;++depth) {
      require(id<project.operators.size() && graph.nodes.at(id).active,"invalid MINMESH material");
      const auto& node=graph.nodes[id];const auto cmd=project.operators[id].command;
      if(cmd==1){require(node.links.size()==1 && node.links[0].has_value(),"unresolved MINMESH material Load");id=*node.links[0];}
      else if(cmd==2 || cmd==3){require(node.inputs.size()==1,"invalid MINMESH material alias");id=node.inputs[0];}
      else return id;
    }
    throw std::runtime_error("cycle or excessive depth in MINMESH material aliases");
  }
  GenMaterial* material(std::size_t id) {
    const auto target=concrete(id);require(operator_info(project.operators[target].command).category=="MATERIAL","MINMESH binding is not a material");
    // Use canonical identities for cluster reuse. Render bindings keep the
    // first link, which MaterialRuntime resolves to the same shader object.
    return &materials->try_emplace(target,GenMaterial{id}).first->second;
  }
  std::shared_ptr<GenMinMesh> convert(std::size_t id) {
    // Mesh_ToMin copies only selected live wedges, their point positions,
    // UV/color attributes and live polygon loops. Its output has no selection.
    const auto input=legacy::evaluate_kernel(project,graph,id,options);
    GenMesh source(*input->kernel);source.Triangulate(8);source.All2Sel(true,MAS_FACE);source.Face2Vert();source.validate();
    auto out=std::make_shared<GenMinMesh>();out->materials=materials;
    std::vector<int> mapping(source.Vert.Count,-1);
    for(int i=0;i<source.Vert.Count;++i)if(source.Vert[i].Select) {
      mapping[i]=out->Vertices.Count;auto& v=*out->Vertices.Add();
      v.Pos.Init(source.buffer[source.Vert[i].First*5]);
      const auto& uv=source.buffer[i*5+4];v.UV[0][0]=uv.x;v.UV[0][1]=uv.y;
      const auto& color=source.buffer[i*5+3];
      const auto lane=[](float x){return std::uint32_t(std::clamp(x*255.0f,0.0f,255.0f));};
      v.Color=lane(color.z)|(lane(color.y)<<8)|(lane(color.x)<<16)|(lane(color.w)<<24);
    }
    // The old native kernel stores material bindings on faces rather than an
    // engine material table. Reconstruct cluster identity from those bindings.
    std::map<std::tuple<int,GenMaterial*,int>,int> clusters;
    auto* default_material=out->Clusters[1].Mtrl;out->Clusters.Count=1;
    for(int i=0;i<source.Face.Count;++i)if(source.Face[i].Material) {
      const auto& in=source.Face[i];auto* m=in.binding ? material(*in.binding) : nullptr;
      const auto key=std::make_tuple(in.Material,m,in.pass);auto found=clusters.find(key);
      if(found==clusters.end()) {
        require(out->Clusters.Count<256,"converted MINMESH cluster limit");const auto index=out->Clusters.Count;
        out->Clusters.Add()->Init(m ? m : default_material,in.pass,in.Material);found=clusters.emplace(key,index).first;
      }
      auto& f=*out->Faces.Add();f.Cluster=found->second;f.Flags=!in.Used;
      const auto start=in.Edge;auto edge=start;
      do {
        require(f.Count<8,"converted MINMESH polygon degree");const auto v=mapping.at(source.GetVertId(edge));
        require(v>=0,"converted MINMESH missing live vertex");f.Vertices[f.Count++]=v;edge=source.NextFaceEdge(edge);
      }while(edge!=start);
    }
    return out;
  }
public:
  Evaluator(const Project& p,const Graph& g,const TextureOptions& o):options(o),project(p),graph(g),cache(p.operators.size()),visiting(cache.size()) {
    require(g.nodes.size()==p.operators.size(),"MINMESH graph size mismatch");
  }
  std::shared_ptr<const GenMinMesh> get(std::size_t id,unsigned depth=0) {
    require(id<cache.size() && graph.nodes[id].active,"invalid or inactive MINMESH operator");
    if(cache[id])return cache[id];
    require(depth<512 && !visiting[id],"cycle or excessive depth in MINMESH graph");visiting[id]=true;
    const auto& op=project.operators[id];const auto& node=graph.nodes[id];
    require(!op.has_animation(),"animated MINMESH initialization is not ported");
    const auto input=[&]() {
      require(node.inputs.size()==1,"MINMESH operator needs one input");return get(node.inputs[0],depth+1);
    };
    if(op.command==1) {
      require(node.links.size()==1 && node.links[0].has_value(),"unresolved MINMESH Load");cache[id]=get(*node.links[0],depth+1);
    }else if(op.command==2 || op.command==3)cache[id]=input();
    else {
      std::shared_ptr<GenMinMesh> mesh;
      switch(op.command) {
      case 0xb5:
        parameters(op,0);require(node.inputs.size()==1,"mesh conversion needs one input");mesh=convert(node.inputs[0]);break;
      case 0x101:
        parameters(op,3);require(node.inputs.empty(),"MINMESH Grid has inputs");
        mesh.reset(MinMesh_Grid(integer(op,0,3),integer(op,1,256,1),integer(op,2,256,1)));break;
      case 0x102:
        parameters(op,13);require(node.inputs.empty(),"MINMESH Cube has inputs");
        mesh.reset(MinMesh_Cube(integer(op,0,256,1),integer(op,1,256,1),integer(op,2,256,1),integer(op,3,15),srt(op,4)));break;
      case 0x104:
        parameters(op,2);require(node.inputs.empty(),"MINMESH Sphere has inputs");
        mesh.reset(MinMesh_Sphere(integer(op,0,256,3),integer(op,1,256,1)));break;
      case 0x133:
        parameters(op,3);require(node.inputs.empty() && op.strings.size()==2,"invalid Font3D layout");
        require(!options.font_file.empty(),"Font3D needs an explicit --font-file");
        mesh=font3d_mesh(options.font_file,op.strings[0],real(op,0),real(op,1),real(op,2));break;
      case 0x128:
        parameters(op,8);mesh=std::make_shared<GenMinMesh>(*input());bone_chain(*mesh,op.parameters);break;
      case 0x12f:
        parameters(op,17);mesh.reset(multiply_mesh(*input(),op.parameters));break;
      case 0x129: {
        parameters(op,3);require(node.inputs.size()==2,"BoneTrain needs a mesh and spline");
        mesh=std::make_shared<GenMinMesh>(*get(node.inputs[0],depth+1));
        bone_train(*mesh,bone_spline(project,graph,node.inputs[1]),real(op,0),integer(op,1,7),real(op,2));break;
      }
      case 0x12c: {
        parameters(op,4);require(node.inputs.size()>=2 && node.inputs.size()<=4,"Pipe needs a spline, straight mesh and optional curve/ring meshes");
        SpatialSplines splines(project,graph);auto env=frame_environment(0);
        auto [spline,pipe]=splines.pipe_curve(node.inputs[0],env);
        const auto straight=get(node.inputs[1],depth+1);
        const auto curved=node.inputs.size()>2 ? get(node.inputs[2],depth+1) : nullptr;
        const auto rings=node.inputs.size()>3 ? get(node.inputs[3],depth+1) : nullptr;
        mesh.reset(pipe_mesh(*spline,*pipe,*straight,curved.get(),rings.get(),op.parameters));break;
      }
      case 0x134: {
        parameters(op,21);require(node.inputs.size()==1 || node.inputs.size()==2,"MINMESH Explode needs a mesh and optional bitmap");
        for(unsigned i=0;i<21;++i)if(i!=15)real(op,i);
        integer(op,15,7);
        auto input_mesh=std::make_shared<GenMinMesh>(*get(node.inputs[0],depth+1));
        const auto bitmap=node.inputs.size()==2 ? evaluate_texture(project,graph,node.inputs[1],options) : nullptr;
        mesh.reset(explode(*input_mesh,bitmap.get(),op.parameters));break;
      }
      case 0x111:
        parameters(op,0);require(!node.inputs.empty(),"MINMESH Add requires inputs");
        mesh=std::make_shared<GenMinMesh>(*get(node.inputs[0],depth+1));
        for(std::size_t i=1;i<node.inputs.size();++i) {
          auto other=get(node.inputs[i],depth+1);mesh->Add(other.get());
        }
        mesh->MergeClusters();break;
      case 0x110:case 0x116:case 0x117: {
        parameters(op,2);std::size_t binding;
        if(op.command==0x117) {
          require(node.inputs.size()==2,"MINMESH MatInput needs mesh and material");binding=node.inputs[1];
        }else {
          require(node.inputs.size()==1 && node.links.size()==1 && node.links[0].has_value(),"invalid MINMESH MatLink");binding=*node.links[0];
        }
        mesh=std::make_shared<GenMinMesh>(*get(node.inputs[0],depth+1));auto* m=material(binding);
        const auto pass=std::bit_cast<std::int32_t>(op.parameters[1]);
        if(op.command==0x116)MinMesh_MatLinkId(mesh.get(),m,std::bit_cast<std::int32_t>(op.parameters[0]),pass);
        else MinMesh_MatLink(mesh.get(),m,integer(op,0,2),pass);
        break;
      }
      case 0x112:case 0x113:case 0x114:case 0x115:case 0x120:case 0x121:
      case 0x122:case 0x124:case 0x125:case 0x12a:case 0x131:case 0x132:
        mesh=std::make_shared<GenMinMesh>(*input());
        switch(op.command) {
        case 0x112:parameters(op,1);MinMesh_SelectAll(mesh.get(),integer(op,0,7));break;
        case 0x113:parameters(op,7);MinMesh_SelectCube(mesh.get(),integer(op,0,11),vector(op,1),vector(op,4));break;
        case 0x114:parameters(op,0);MinMesh_DeleteFaces(mesh.get());break;
        case 0x115:parameters(op,0);MinMesh_Invert(mesh.get());break;
        case 0x120:case 0x121: {
          parameters(op,op.command==0x120 ? 10 : 12);const auto mask=integer(op,0,0x3ff);
          require((mask&3)<=2 && ((mask>>2)&7)<=2 && ((mask>>5)&7)<=2 && (mask>>8)<=2,"invalid MINMESH transform mode");
          MinMesh_TransformEx(mesh.get(),mask,srt(op,1));break;
        }
        case 0x122:parameters(op,2);MinMesh_ExtrudeNormal(mesh.get(),integer(op,0,2),real(op,1));break;
        case 0x124:parameters(op,13);MinMesh_Perlin(mesh.get(),integer(op,0,2),srt(op,1),vector(op,10));break;
        case 0x125:parameters(op,8);require(real(op,6)>0,"MINMESH Bend requires positive length");MinMesh_Bend2(mesh.get(),vector(op,0),vector(op,3),real(op,6),real(op,7));break;
        case 0x12a:parameters(op,0);MinMesh_Triangulate(mesh.get());break;
        case 0x131:parameters(op,1);MinMesh_Center(mesh.get(),integer(op,0,7));break;
        case 0x132:parameters(op,1);MinMesh_SelectLogic(mesh.get(),integer(op,0,2));break;
        }
        break;
      default:throw std::runtime_error("MINMESH operator "+std::to_string(id)+" ("+std::string(operator_info(op.command).name)+") is not ported");
      }
      mesh->materials=materials;mesh->validate();
      const auto bytes=mesh->bytes();require(bytes<=256u*1024*1024-allocated,"MINMESH cache exceeds 256 MiB limit");allocated+=bytes;cache[id]=mesh;
    }
    visiting[id]=false;return cache[id];
  }
};
}
std::shared_ptr<const Mesh> evaluate_kernel(const Project& project,const Graph& graph,std::size_t id,const TextureOptions& options) {
  const auto core=Evaluator(project,graph,options).get(id);
  return std::make_shared<Mesh>(export_mesh(std::make_shared<GenMinMesh>(*core)));
}
} // namespace wz3::legacy::minimal
