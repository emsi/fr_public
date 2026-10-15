// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Shader expressions follow materials/material11.psh, including the original
// LRP operand order (which differs from the explanatory header comment).
#include "wz3/material.hpp"
#include "mesh_kernel.hpp"
#include <cmath>
#include <stdexcept>

namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::string texture(const MaterialShader& s,unsigned slot) {
  require(s.textures[slot].has_value(),"combiner references an unbound texture");return "sample"+std::to_string(slot);
}
std::string alpha(const MaterialShader& s,unsigned source) {
  if(source==0 || source==3)return "1.0";
  if(source>=4 && source<=7)return texture(s,source-4)+".a";
  if(source>=8 && source<=11)return "material_colors["+std::to_string(source-8)+"].a";
  if(source==12)return "0.5";
  if(source==13)return "0.0";
  throw std::runtime_error("alpha source needs an unsupported lighting register");
}
std::string source(const MaterialShader& s,unsigned index) {
  switch(index) {
  case 1:return "material_colors[0].rgb";case 2:return "material_colors[1].rgb";
  case 3:return texture(s,0)+".rgb";case 4:return texture(s,1)+".rgb";
  case 7:return "material_colors[2].rgb";case 8:return "material_colors[3].rgb";
  case 9:return texture(s,2)+".rgb";case 10:return texture(s,3)+".rgb";
  case 11:return "material_vertex_color.rgb";
  default:throw std::runtime_error("RGB source needs an unsupported lighting register");
  }
}
}
std::string material_fragment_shader(const MaterialShader& s) {
  require(!s.material20,"Material 2.0 GPU shaders are not ported");
  require(!(s.words[2]&7),"lighting requires the scene mesh shader");
  require(!(s.words[1]&1) && ((s.words[1]>>8)&15)<=3,"unsupported material special flags");
  std::string code="#version 300 es\nprecision highp float;\nin vec2 material_uv[4];\nin vec4 material_vertex_color;\nuniform vec4 material_colors[4];\nout vec4 color;\n";
  for(unsigned i=0;i<4;++i)if(s.textures[i])code+="uniform highp sampler2D material_tex"+std::to_string(i)+";\n";
  code+="void main() {\nvec3 current=vec3(0.0);\n";
  for(unsigned i=0;i<4;++i)if(s.textures[i])code+="vec4 sample"+std::to_string(i)+"=texture(material_tex"+std::to_string(i)+",material_uv["+std::to_string(i)+"]);\n";
  bool initialized=false;
  for(unsigned i=0;i<13;++i) {
    const auto flags=s.words[8+i],op=(flags>>4)&15;
    require(!(flags&~0xffu) && op<=12,"unsupported material combiner operation");if(!op)continue;
    auto value=source(s,i);const auto fade=alpha(s,flags&15);
    if((flags&15) && op!=9)value="("+value+"*"+fade+")";
    require(initialized || op==1,"material combiner reads an uninitialized accumulator");initialized=true;
    switch(op) {
    case 1:code+="current="+value+";\n";break;
    case 2:code+="current+="+value+";\n";break;
    case 3:code+="current-="+value+";\n";break;
    case 4:case 5:case 6:case 7:code+="current=current*"+value+"*"+std::to_string(1u<<(op-4))+".0;\n";break;
    case 8:code+="current=current*(vec3(1.0)-"+value+")+"+value+";\n";break;
    case 9:code+="current=mix("+value+",current,"+fade+");\n";break;
    case 10:code+="current="+value+"-current;\n";break;
    case 11:code+="current=vec3(dot(current,"+value+"));\n";break;
    case 12:code+="current=clamp(current*"+value+",0.0,1.0);\n";break;
    }
  }
  require(initialized,"empty material pixel combiner");const auto ac=s.words[23];require(!(ac&~0x3ffu),"unsupported alpha combiner flags");
  std::string a;
  if(((ac>>4)&15)==14)a="("+texture(s,0)+".a+"+texture(s,1)+".a)*material_colors[0].a";
  else {
    a=alpha(s,ac&15);if(ac&0x100)a="(1.0-"+a+")";
    if((ac>>4)&15) {auto b=alpha(s,(ac>>4)&15);if(ac&0x200)b="(1.0-"+b+")";a="("+a+"*"+b+")";}
  }
  code+="float alpha_value=clamp("+a+",0.0,1.0);\n";
  if(s.words[0]&1)code+="if(alpha_value<=128.0/255.0)discard;\n";
  return code+"color=vec4(clamp(current,0.0,1.0),alpha_value);\n}\n";
}
std::array<float,16> material_uv_transform(const MaterialShader& s,unsigned slot) {
  require(!s.material20,"Material 2.0 UV binding is not ported");
  require(slot<4,"invalid material texture slot");const auto flags=s.words[4+slot];
  require(!(flags&0x4030),"secondary/projective UV input is not ported");
  const auto real=[&](unsigned i) {const auto v=as_float(s.words[i]);require(std::isfinite(v),"nonfinite material UV transform");return v;};
  std::array<float,16> matrix={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  switch((flags>>12)&3) {
  case 1:matrix[0]=matrix[5]=real(24+slot);break;
  case 2: {
    float values[9];for(unsigned i=0;i<9;++i)values[i]=real(34+i);legacy::sMatrix m;m.InitSRT(values);
    matrix={m.i.x,m.i.y,m.i.z,m.i.w,m.j.x,m.j.y,m.j.z,m.j.w,m.k.x,m.k.y,m.k.z,m.k.w,m.l.x,m.l.y,m.l.z,m.l.w};break;
  }
  case 3: {
    const auto a=real(45),sine=float(std::sin(double(a))),cosine=float(std::cos(double(a)));
    matrix={real(43)*cosine,-real(43)*sine,0,0,real(44)*sine,real(44)*cosine,0,0,0,0,1,0,real(46),real(47),0,1};break;
  }
  }
  for(auto value:matrix)require(std::isfinite(value),"nonfinite material UV matrix");
  return matrix;
}
std::array<float,9> material_uv_matrix(const MaterialShader& s,unsigned slot) {
  const auto m=material_uv_transform(s,slot);return {m[0],m[1],0,m[4],m[5],0,m[12],m[13],1};
}
} // namespace wz3
