// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Record the archived Init/Exec handlers; GPU classes capture constructor and
// UpdatePara arguments. This oracle verifies bindings and passes, not shading.
#include "material20_reference.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace reference {
namespace {
using sInt=int;
constexpr int sINVALID=-1,KC_BITMAP=1,KC_MATERIAL=2,MPP_STATIC=1,MPP_SHADOW=2;
constexpr int ENGU_BASE=0,ENGU_PRELIGHT=1,ENGU_AMBIENT=2,ENGU_SHADOW=3,ENGU_LIGHT=4,ENGU_POSTLIGHT=5;
constexpr unsigned sMBF_ZREAD=0x200,sMBF_SHADOWMASK=0x20,sMBF_ZONLY=0x40000,sMBF_NOTEXTURE=0x10000,sMBF_NONORMAL=0x20000;
constexpr int sMCS_COLOR0=1,sMCOA_SET=0x10,sFALSE=0;
#define __stdcall
#define REALMAT 1
#define sVERIFY(value) do {if(!(value))throw std::runtime_error("archived material assertion");} while(0)
void sCopyMem(void* to,const void* from,std::size_t bytes) {std::memcpy(to,from,bytes);}
struct sMaterial20Para {std::uint32_t Flags,rest[41];};
static_assert(sizeof(sMaterial20Para)==42*4);
struct KObject {int ClassId=0;virtual ~KObject()=default;void Release() {}};
struct GenBitmap:KObject {
  int Texture=-1,Format=0;
  void MakeTexture(int) {throw std::runtime_error("fixture texture missing");}
};
struct KEnvironment {};
struct KOp {
  std::array<std::uint32_t,42> parameters{};
  std::array<KOp*,7> links{};std::array<KOp*,3> inputs{};KObject* Cache=nullptr;
  std::uint32_t* GetAnimPtrU(unsigned i) {return parameters.data()+i;}
  KOp* GetLink(unsigned i) {return links.at(i);}
  KObject* GetLinkCache(unsigned i) {return links.at(i)->Cache;}
  KOp* GetInput(unsigned i) {return inputs.at(i);}
  void ExecInputs(KEnvironment*) {}
};
struct Material20Insert {};
struct Targets {
  void Add(unsigned id,int w,int h) {sVERIFY(id==0x10000 && w==0 && h==0);}
};
Targets targets;Targets* RenderTargetManager=&targets;
struct sMaterial {wz3::MaterialShader shader;virtual ~sMaterial()=default;};
struct sMaterial20Base:sMaterial {
  sMaterial20Base(const sMaterial20Para& p,const int* textures,wz3::Material20Stage stage) {
    auto& m=shader.material20.emplace();m.stage=stage;
    std::memcpy(m.parameters.data(),&p,42*4);m.compiled_parameters=m.parameters;
    for(unsigned i=0;i<7;++i)if(textures[i]!=sINVALID)m.textures[i]=textures[i];
  }
  void UpdatePara(const sMaterial20Para& p) {std::memcpy(shader.material20->parameters.data(),&p,42*4);}
};
struct sMaterial20ZFill:sMaterial20Base {
  sMaterial20ZFill(const sMaterial20Para& p,int* t):sMaterial20Base(p,t,wz3::Material20Stage::depth) {}
};
struct sMaterial20Tex:sMaterial20Base {
  sMaterial20Tex(const sMaterial20Para& p,int* t,int ambient):sMaterial20Base(p,t,wz3::Material20Stage::texture) {sVERIFY(!ambient);}
};
struct sMaterial20VColor:sMaterial20Base {
  sMaterial20VColor(const sMaterial20Para& p,int* t):sMaterial20Base(p,t,wz3::Material20Stage::ambient) {}
};
struct sMaterial20Light:sMaterial20Base {
  sMaterial20Light(const sMaterial20Para& p,int* t,unsigned target):sMaterial20Base(p,t,wz3::Material20Stage::light) {sVERIFY(target==0x10000);}
};
struct sMaterial20Envi:sMaterial20Base {
  sMaterial20Envi(const sMaterial20Para& p,int* t):sMaterial20Base(p,t,wz3::Material20Stage::environment) {}
};
struct sMaterial11:sMaterial {
  unsigned BaseFlags=0;std::array<unsigned,13> Combiner{};
  void Compile() {shader.words[0]=BaseFlags;std::copy(Combiner.begin(),Combiner.end(),shader.words.begin()+8);}
};
struct Pass {sMaterial* Mtrl;int Usage,Program;};
struct Passes {std::vector<Pass> values;int Count=0;Pass& operator[](unsigned i){return values.at(i);}};
struct GenMaterial:KObject {
  reference::Passes Passes;Material20Insert* Insert=nullptr;
  GenMaterial() {ClassId=KC_MATERIAL;}
  ~GenMaterial() {for(auto& pass:Passes.values)delete pass.Mtrl;}
  void AddPass(sMaterial* m,int use,int program) {Passes.values.push_back({m,use,program});++Passes.Count;}
};
#include "material20_oracle.inc"
}
wz3::Material material20(std::span<const std::uint32_t> parameters,const std::array<int,7>& links,
                        const std::array<int,3>& inputs,std::span<const std::uint32_t> update) {
  sVERIFY(parameters.size()==42 && (update.empty() || update.size()==42));
  KOp op;std::copy(parameters.begin(),parameters.end(),op.parameters.begin());
  std::array<KOp,10> nodes;std::array<GenBitmap,10> bitmaps;
  for(unsigned i=0;i<10;++i) {
    const int id=i<7 ? links[i] : inputs[i-7];
    if(id==-1)continue;
    auto& node=nodes[i];auto& bitmap=bitmaps[i];node.Cache=&bitmap;bitmap.ClassId=id==-2 ? 0 : KC_BITMAP;bitmap.Texture=id;
    if(i<7)op.links[i]=&node;else op.inputs[i-7]=&node;
  }
  const std::unique_ptr<GenMaterial> result(Init_Material_Material20(&op));op.Cache=result.get();
  if(!update.empty()) {std::copy(update.begin(),update.end(),op.parameters.begin());KEnvironment env;Exec_Material_Material20(&op,&env);}
  wz3::Material out;out.material20_insert=result->Insert!=nullptr;
  for(const auto& pass:result->Passes.values)
    out.passes.push_back({pass.Mtrl->shader,wz3::MaterialUsage(pass.Usage),wz3::MaterialProgram(pass.Program),0,1,1});
  return out;
}
}
