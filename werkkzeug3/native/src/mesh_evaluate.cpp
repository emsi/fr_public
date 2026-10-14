// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "mesh_kernel.hpp"
#include <bit>

namespace wz3::legacy {
namespace {
void require(const Operator& op,std::size_t count) {
  if(op.parameters.size()!=count) throw std::runtime_error("unsupported mesh parameter layout");
}
float real(const Operator& op,unsigned i) {
  const auto v=as_float(op.parameters.at(i));
  if(!std::isfinite(v) || std::abs(v)>1000000) throw std::runtime_error("invalid mesh numeric parameter");
  return v;
}
unsigned integer(const Operator& op,unsigned i,unsigned max=255,unsigned min=0) {
  const auto v=op.parameters.at(i);
  if(v<min || v>max) throw std::runtime_error("unsupported mesh integer parameter");
  return v;
}
sVector vector(const Operator& op,unsigned i) {return {real(op,i),real(op,i+1),real(op,i+2),0};}
sVector pair(const Operator& op,unsigned i) {return {real(op,i),real(op,i+1),0,0};}
sFSRT srt(const Operator& op,unsigned i) {
  sFSRT value{};value.s=vector(op,i);value.r=vector(op,i+3);value.t=vector(op,i+6);
  for(unsigned c=0;c<9;++c)value.v[c]=real(op,i+c);
  return value;
}
class Evaluator {
  const Project& project; const Graph& graph;
  std::vector<std::shared_ptr<const GenMesh>> cache;
  std::vector<bool> visiting;
  std::size_t allocated{};
  bool material(std::size_t id) const {
    for(unsigned depth=0;depth<512;++depth) {
      if(id>=project.operators.size() || !graph.nodes.at(id).active)return false;
      const auto command=project.operators[id].command;const auto& node=graph.nodes[id];
      if(command==1) {if(node.links.size()!=1 || !node.links[0])return false;id=*node.links[0];}
      else if(command==2 || command==3) {if(node.inputs.size()!=1)return false;id=node.inputs[0];}
      else return operator_info(command).category=="MATERIAL";
    }
    return false;
  }
public:
  Evaluator(const Project& p,const Graph& g):project(p),graph(g),cache(p.operators.size()),visiting(cache.size()) {}
  std::shared_ptr<const GenMesh> get(std::size_t id,unsigned depth=0) {
    if(id>=cache.size() || !graph.nodes.at(id).active)throw std::runtime_error("invalid or inactive mesh operator");
    if(cache[id])return cache[id];
    if(depth>512 || visiting[id])throw std::runtime_error("cycle or excessive depth in mesh graph");
    visiting[id]=true;
    const auto& op=project.operators[id];const auto& node=graph.nodes[id];
    if(!op.animations.empty())throw std::runtime_error("animated mesh operators are not ported");
    const auto input=[&]() {
      if(node.inputs.size()!=1)throw std::runtime_error("mesh operator "+std::to_string(id)+" ("+std::string(operator_info(op.command).name)+") input signature is not ported");
      return get(node.inputs[0],depth+1);
    };
    if(op.command==1) {
      if(node.links.size()!=1 || !node.links[0])throw std::runtime_error("unresolved mesh Load");
      cache[id]=get(*node.links[0],depth+1);
    }else if(op.command==2 || op.command==3)cache[id]=input();
    else {
      std::shared_ptr<GenMesh> mesh;
      if(op.command>=0x81 && op.command<=0x84) {
        if(!node.inputs.empty())throw std::runtime_error("mesh primitive has unexpected inputs");
        if(op.command==0x81) {require(op,13);mesh.reset(Mesh_Cube(integer(op,0,256,1),integer(op,1,256,1),integer(op,2,256,1),integer(op,3,15),srt(op,4)));}
        else if(op.command==0x82) {
          if(op.parameters.size()!=4 && op.parameters.size()!=5)throw std::runtime_error("unsupported Cylinder layout");
          const auto tx=integer(op,0,256,3);
          mesh.reset(Mesh_Cylinder(tx,integer(op,1,256,1),integer(op,2,3),integer(op,3,256,1),op.parameters.size()==5 ? integer(op,4,tx-1) : 0));
        }else if(op.command==0x83) {
          require(op,7);const auto arc=real(op,5);
          if(arc<=0 || arc>1)throw std::runtime_error("Torus arc outside (0,1]");
          mesh.reset(Mesh_Torus(integer(op,0,256,3),integer(op,1,256,3),real(op,2),real(op,3),real(op,4),arc,integer(op,6,3)));
        }else {require(op,2);mesh.reset(Mesh_Sphere(integer(op,0,256,4),integer(op,1,256,1)));}
      }else if(op.command==0x9d) {
        require(op,3);if(!node.inputs.empty())throw std::runtime_error("Grid has unexpected inputs");
        mesh.reset(Mesh_Grid(integer(op,0,3),integer(op,1,256,1),integer(op,2,256,1)));
      }else if(op.command==0x92) {
        require(op,0);if(node.inputs.empty())throw std::runtime_error("mesh Add requires inputs");
        mesh=std::make_shared<GenMesh>();mesh->Init(sGMF_DEFAULT,0);
        for(auto source:node.inputs)mesh->append(*get(source,depth+1));
      }else {
        mesh=std::make_shared<GenMesh>(*input());
        switch(op.command) {
        case 0x86: {
          require(op,8);Mesh_SelectCube(mesh.get(),integer(op,0,0xffffff),integer(op,1,7),vector(op,2),vector(op,5));break;
        }
        case 0xa8: {
          require(op,8);const auto size=vector(op,5);
          if(size.x<=0 || size.y<=0 || size.z<=0)throw std::runtime_error("SelectSphere requires positive dimensions");
          Mesh_SelectSphere(mesh.get(),integer(op,0,0xffffff),integer(op,1,7),vector(op,2),size);break;
        }
        case 0xa9:
          require(op,3);Mesh_SelectFace(mesh.get(),integer(op,0),integer(op,1,3),integer(op,2,kernel_limit));break;
        case 0x94:
          require(op,4);Mesh_SelectRandom(mesh.get(),integer(op,0,0xffffff),integer(op,1,3),integer(op,2),std::bit_cast<std::int32_t>(op.parameters[3]));break;
        case 0x87:
          require(op,3);Mesh_Subdivide(nullptr,mesh.get(),integer(op,0),real(op,1),integer(op,2,8,1));break;
        case 0x8c:
          require(op,3);Mesh_Triangulate(mesh.get(),integer(op,0),integer(op,1,256,3),integer(op,2,2));break;
        case 0xa1:
          require(op,4);Mesh_SelectLogic(mesh.get(),integer(op,0,0xffffff),integer(op,1,0xffffff),integer(op,2,0xffffff),integer(op,3,63));break;
        case 0x9a:
          require(op,13);Mesh_Extrude(nullptr,mesh.get(),integer(op,0),integer(op,1,65535),integer(op,2,63),integer(op,3,4096,1),vector(op,4),vector(op,7),vector(op,10));break;
        case 0x88: case 0x89: {
          require(op,op.command==0x88 ? 10 : 12);
          const auto mask=integer(op,0),src=op.command==0x88 ? 0 : integer(op,10,5),dst=op.command==0x88 ? 0 : integer(op,11,5);
          if(mesh->VertMap(src)<0 || mesh->VertMap(dst)<0)throw std::runtime_error("mesh transform attribute not present");
          if(src==sGMI_NORMAL || src==sGMI_TANGENT)mesh->NeedAllNormals();
          if(mask)mesh->Mask2Sel(mask<<16);else mesh->All2Sel();
          sMatrix m;const auto value=srt(op,1);m.InitSRT(value.v);mesh->TransVert(m,src,dst);
          if(dst<=2)mesh->GotNormals=false;
          break;
        }
        case 0x96: {
          require(op,2);const auto mask=integer(op,0);
          if(node.links.size()!=1 || !node.links[0] || !material(*node.links[0]))throw std::runtime_error("invalid mesh material link");
          for(int f=0;f<mesh->Face.Count;++f)if(mesh->Face[f].Material && (!mask || (mesh->Face[f].Mask&mask))) {
            mesh->Face[f].binding=*node.links[0];mesh->Face[f].pass=std::bit_cast<std::int32_t>(op.parameters[1]);
          }
          break;
        }
        case 0xa3:
          require(op,0);
          for(int i=0;i<mesh->Edge.Count;++i)for(unsigned j=0;j<2;++j)mesh->Edge[i].Temp[j]=mesh->GetVertId(mesh->Edge[i].Next[j]);
          for(int i=0;i<mesh->Edge.Count;++i)for(unsigned j=0;j<2;++j) {
            auto& e=mesh->Edge[i];std::swap(e.Next[j],e.Prev[j]);e.Vert[j]=e.Temp[j];
          }
          mesh->GotNormals=false;break;
        case 0xac:
          require(op,1);Mesh_SmoothAngle(mesh.get(),real(op,0));break;
        case 0xab:
          require(op,8);if(real(op,6)<=0)throw std::runtime_error("Bend 2 requires positive length");
          Mesh_Bend2(mesh.get(),vector(op,0),vector(op,3),real(op,6),real(op,7));break;
        case 0xa0: {
          require(op,24);const auto mode=integer(op,23,7);
          if(!(mode&2) && real(op,21)==real(op,22))throw std::runtime_error("Bend requires a nonzero range");
          Mesh_Bend(nullptr,mesh.get(),integer(op,0),srt(op,1),srt(op,10),pair(op,19),pair(op,21),mode);break;
        }
        case 0xad:
          require(op,9);if(op.parameters[8]==1 && real(op,7)<=0)throw std::runtime_error("point color light requires positive range");
          Mesh_Color(mesh.get(),vector(op,0),pair(op,3),op.parameters[5],real(op,6),real(op,7),integer(op,8,3));break;
        case 0xb0:
          require(op,1);Mesh_ShadowEnable(mesh.get(),integer(op,0,1));break;
        case 0xa5: {
          require(op,11);const auto mask=integer(op,0),type=integer(op,10,3);
          sMatrix m;const auto value=srt(op,1);m.InitSRT(value.v);
          if(type==0 || type==3)mesh->CubicProjection(m,mask,sGMI_UV0,type==0);
          else for(int v=0;v<mesh->Vert.Count;++v) {
            sVector p;p.Rotate34(m,mesh->VertPos(v));auto& uv=mesh->buffer[v*5+4];
            uv.x=0.5f-std::atan2(double(p.x),double(p.z))/sPI2F;
            uv.y=type==1 ? -p.y : 0.5f-std::atan2(double(p.y),std::sqrt(double(p.x*p.x+p.z*p.z)))/sPI;
          }
          break;
        }
        case 0xa6: {
          require(op,2);integer(op,0);const auto which=integer(op,1);
          if(which&~135u)throw std::runtime_error("unsupported Center flags");
          sAABox box;mesh->CalcBBox(box);mesh->All2Sel(true,MAS_VERT);sMatrix m;m.Init();
          for(unsigned c=0;c<3;++c)if(which&(1u<<c))m.l[c]=c==1 && (which&128) ? -box.Min[c] : -0.5f*(box.Min[c]+box.Max[c]);
          mesh->TransVert(m);break;
        }
        case 0x95: {
          require(op,17);const auto count=integer(op,9,4096,1),mode=integer(op,10,1);
          const auto extrude=real(op,16);
          if(extrude)mesh->NeedAllNormals();
          auto out=std::make_shared<GenMesh>();out->Init(sGMF_DEFAULT,0);
          const auto value=srt(op,0);const auto turns=vector(op,13);
          sMatrix global,local,step,local_step;global.Init();local.Init();step.InitSRT(value.v);local_step.InitEulerPI2(turns);
          for(unsigned copy=0;copy<count;++copy) {
            const auto start=out->Vert.Count;out->append(*mesh);out->All2Sel(false,MAS_VERT);
            for(int v=start;v<out->Vert.Count;++v)out->Vert[v].Select=true;
            if(extrude)out->ExtrudeNormal(copy*extrude);
            sMatrix m;m.MulA(local,global);out->TransVert(m);global.MulA(step);local.MulA(local_step);
            if(mode&1) {m.Init();m.l.x=copy*real(op,11);m.l.y=copy*real(op,12);out->TransVert(m,sGMI_UV0,sGMI_UV0);}
          }
          mesh=std::move(out);break;
        }
        default:throw std::runtime_error("mesh operator "+std::to_string(id)+" ("+std::string(operator_info(op.command).name)+") is not ported");
        }
      }
      mesh->validate();
      if(mesh->bytes()>256*1024*1024-allocated)throw std::runtime_error("indexed mesh cache exceeds 256 MiB limit");
      allocated+=mesh->bytes();cache[id]=mesh;
    }
    visiting[id]=false;return cache[id];
  }
};
}

Mesh export_mesh(std::shared_ptr<GenMesh> mesh) {
  mesh->validate();mesh->NeedAllNormals();mesh->validate();
  Mesh out;out.vertices.reserve(mesh->Vert.Count);
  for(int v=0;v<mesh->Vert.Count;++v) {
    const auto& p=mesh->buffer[v*5];const auto& uv=mesh->buffer[v*5+4];
    const auto& tangent=mesh->buffer[v*5+2];const auto& color=mesh->buffer[v*5+3];
    out.vertices.push_back({{p.x,p.y,p.z},{uv.x,uv.y,uv.z},mesh->Vert[v].Mask,std::uint32_t(mesh->Vert[v].First),
                            {tangent.x,tangent.y,tangent.z},{color.x,color.y,color.z,color.w}});
  }
  for(int i=0;i<mesh->Face.Count;++i)if(mesh->Face[i].Material) {
    std::vector<std::uint32_t> corners;std::vector<std::uint8_t> creases;
    auto e=mesh->Face[i].Edge;const auto start=e;
    do {corners.push_back(mesh->GetVertId(e));creases.push_back(mesh->Edge[e/2].Crease);e=mesh->NextFaceEdge(e);}while(e!=start);
    MeshFace f;f.mask=mesh->Face[i].Mask;f.material=mesh->Face[i].binding;f.pass=mesh->Face[i].pass;f.casts_shadow=mesh->Face[i].Used;
    if(corners.size()<=4) {
      f.count=corners.size();std::copy(corners.begin(),corners.end(),f.vertices.begin());std::copy(creases.begin(),creases.end(),f.crease.begin());out.faces.push_back(f);
    }else for(std::size_t c=1;c+1<corners.size();++c) {
      f.count=3;f.vertices={corners[0],corners[c],corners[c+1],0};
      f.crease={std::uint8_t(c==1 ? creases[0] : 0),creases[c],std::uint8_t(c+2==corners.size() ? creases.back() : 0),0};out.faces.push_back(f);
    }
  }
  out.kernel=std::move(mesh);validate_mesh(out);return out;
}
std::shared_ptr<const Mesh> evaluate_kernel(const Project& project,const Graph& graph,std::size_t id) {
  const auto core=Evaluator(project,graph).get(id);
  return std::make_shared<Mesh>(export_mesh(std::make_shared<GenMesh>(*core)));
}
} // namespace wz3::legacy
