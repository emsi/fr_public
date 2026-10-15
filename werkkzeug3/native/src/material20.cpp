// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Translation of materials/material20_{zfill,tex,vcolor,light,envi}.{vsh,psh}
// and their Set methods. The REALMAT path used by the archived player.
#include "wz3/material20.hpp"
#include "wz3/lighting.hpp"
#include "mesh_kernel.hpp"
#include <algorithm>
#include <cmath>

namespace wz3 {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
float real(const Material20Shader& s,unsigned i) {
  const auto v=as_float(s.parameters[i]);require(std::isfinite(v),"nonfinite Material 2.0 constant");return v;
}
std::array<float,4> color(std::uint32_t c){return {float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,float(c>>24)/255};}
}
std::uint32_t material20_sampler_flags(const Material20Shader& s,unsigned slot) {
  require(slot<8,"invalid Material 2.0 texture slot");
  return slot==7 ? 0x100 : s.compiled_parameters[(slot<4 ? 8 : 12)+slot];
}
std::array<float,9> material20_uv_matrix(const Material20Shader& s,unsigned slot) {
  require(slot<7,"invalid Material 2.0 UV slot");
  const auto mode=(material20_sampler_flags(s,slot)>>12)&15;
  // Envi samples its bump texture with raw UV0, irrespective of bump flags.
  if(s.stage==Material20Stage::environment)return {1,0,0,0,1,0,0,0,1};
  if(mode==1) {const auto scale=real(s,(slot<4 ? 12 : 16)+slot);return {scale,0,0,0,scale,0,0,0,1};}
  if(mode==2) {const auto m=scene_srt(std::span(s.parameters).subspan(24,9));return {m[0],m[1],0,m[4],m[5],0,m[12],m[13],1};}
  if(mode==3) {
    const auto sx=real(s,33),sy=real(s,34),angle=real(s,35),sn=std::sin(angle),cs=std::cos(angle);
    return {sx*cs,-sx*sn,0,sy*sn,sy*cs,0,real(s,36),real(s,37),1};
  }
  return {1,0,0,0,1,0,0,0,1};
}
std::array<float,12> material20_color_matrix(const Material20Shader& s) {
  using namespace legacy;
  sVector gray;gray.Init4(1,1,1,0);gray.Unit3();sMatrix m,m2;
  m.InitRot(gray,real(s,38)*sPI2F);
  const auto saturation=real(s,39);
  m2.i.Init4(.3086f,.3086f,.3086f,0);m2.i.Scale3(1-saturation);m2.i.x+=saturation;
  m2.j.Init4(.6094f,.6094f,.6094f,0);m2.j.Scale3(1-saturation);m2.j.y+=saturation;
  m2.k.Init4(.0820f,.0820f,.0820f,0);m2.k.Scale3(1-saturation);m2.k.z+=saturation;
  m2.l.Init4(0,0,0,1);m.MulA(m2);
  const auto contrast=real(s,40),shift=.5f*(1-contrast)+real(s,41);
  m2.Init();m2.i.x=m2.j.y=m2.k.z=contrast;m2.l.Init4(shift,shift,shift,1);m.MulA(m2);
  return {m.i.x,m.j.x,m.k.x,m.l.x,m.i.y,m.j.y,m.k.y,m.l.y,m.i.z,m.j.z,m.k.z,m.l.z};
}
Material20Lighting material20_lighting(const Material20Shader& s,const SceneMatrix& model,const SceneCamera& camera,const SceneLight& light) {
  require(std::isfinite(light.range) && light.range>0 && std::isfinite(light.amplify),"invalid Material 2.0 light");
  Material20Lighting out;out.directional=light.flags&4;
  const auto pos=light_model_vector(model,light.position,true);
  out.position=out.directional ? light_model_vector(model,light.direction,false) : pos;
  out.eye=light_model_vector(model,{camera.world[12],camera.world[13],camera.world[14]},true);
  const auto scale=1.f/light.range;
  out.attenuation={-pos[0]*scale,-pos[1]*scale,-pos[2]*scale,scale};
  auto lc=color(light.color);out.diffuse=color(s.parameters[1]);out.specular=color(s.parameters[2]);
  for(unsigned i=0;i<4;++i){lc[i]*=light.amplify;out.diffuse[i]*=lc[i];out.specular[i]*=lc[i];}
  out.specular[3]=real(s,3);
  // The original doubles the exponent too, not only the RGB intensities.
  if(s.parameters[0]&0x40)for(unsigned i=0;i<4;++i){out.diffuse[i]*=2;out.specular[i]*=2;}
  return out;
}
SceneMatrix material20_environment_matrix(const Material20Shader& s,const SceneMatrix& model,const SceneCamera& camera) {
  if(s.parameters[18]&0x10000)return model;
  auto view=scene_identity();for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)view[c*4+r]=camera.world[r*4+c];
  return scene_combine(view,model); // Only the 3x3 rotation is consumed.
}
Material20Source material20_source(const Material20Shader& s) {
  Material20Source out;
  const auto flags=s.compiled_parameters[0];
  const auto add=[&](unsigned slot){if(s.textures[slot])out.samplers.push_back(slot);};
  switch(s.stage) {
    case Material20Stage::depth:add(3);break;
    case Material20Stage::texture:for(unsigned i=0;i<3;++i)add(i);break;
    case Material20Stage::ambient:break;
    case Material20Stage::light:
      out.samplers.push_back(7);add(4);add(5);
      require((flags&0x81)!=0x81 || s.textures[4] || s.textures[5],"Material 2.0 specular map has no bump texture");break;
    case Material20Stage::environment:
      require(bool(s.textures[6]),"Material 2.0 environment texture is absent");add(6);
      if((s.compiled_parameters[18]&0x20000) || (flags&0x80)) {
        require(bool(s.textures[4]),"Material 2.0 environment bump texture is absent");
        require(s.compiled_parameters[18]&0x20000,"Material 2.0 unbumped environment specular UVs are undefined in the original shader");add(4);
      }break;
    default:throw std::runtime_error("unknown Material 2.0 stage");
  }
  out.vertex=R"(#version 300 es
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=3) in vec3 normal;
layout(location=4) in vec3 tangent;
uniform mat4 view_projection;
uniform mat3 uv_transform[3];
uniform vec3 flip_texture;
out vec2 material_uv[3];
invariant gl_Position;
)";
  out.fragment=R"(#version 300 es
precision highp float;
in vec2 material_uv[3];
out vec4 color;
)";
  for(const auto slot:out.samplers)out.fragment+="uniform highp sampler2D material_tex"+std::to_string(slot)+";\n";
  const auto sample=[&](unsigned slot){
    const auto it=std::find(out.samplers.begin(),out.samplers.end(),slot);
    require(it!=out.samplers.end(),"missing Material 2.0 sampler");
    return "texture(material_tex"+std::to_string(slot)+",material_uv["+std::to_string(it-out.samplers.begin())+"])";
  };
  std::string vs="void main(){\ngl_Position=view_projection*vec4(position,1.0);\n";
  for(std::size_t i=0;i<out.samplers.size();++i)if(out.samplers[i]!=7) {
    const auto n=std::to_string(i);
    vs+="material_uv["+n+"]=(uv_transform["+n+"]*vec3(uv,1.0)).xy;\nif(flip_texture["+n+"]>0.5)material_uv["+n+"].y=1.0-material_uv["+n+"].y;\n";
  }
  std::string ps="void main(){\n";
  if(s.stage==Material20Stage::depth) {
    out.fragment+="uniform float alpha_burn;\n";
    ps+="color="+std::string(s.textures[3] ? sample(3) : "vec4(0.0,0.0,0.0,1.0)")+";\n";
    if(s.textures[3]) {
      if(flags&0x100)ps+="color.a=clamp(pow(clamp(color.a-alpha_burn,0.0,1.0),0.390625)*8.0,0.0,1.0);\n";
      ps+="if(color.a<=128.0/255.0)discard;\n";
    }
  }else if(s.stage==Material20Stage::texture) {
    out.fragment+="uniform vec4 color_matrix[3];\n";
    ps+="color="+std::string(s.textures[0] ? sample(0) : "vec4(1.0,1.0,1.0,0.0)")+";\n";
    if(flags&0x200)ps+="vec4 main_color=vec4(color.rgb,1.0);\ncolor.rgb=clamp(vec3(dot(main_color,color_matrix[0]),dot(main_color,color_matrix[1]),dot(main_color,color_matrix[2])),0.0,1.0);\n";
    for(unsigned i=1;i<3;++i)if(s.textures[i]) {
      const auto mode=(s.compiled_parameters[8+i]>>16)&15;const auto tex=sample(i);
      if(mode<3)ps+="color.rgb*="+tex+".rgb*"+std::to_string(1u<<mode)+".0;\n";
      else if(mode==3)ps+="color.rgb+="+tex+".rgb;\n";
      else if(mode==4)ps+="color.rgb=mix(color.rgb,vec3(1.0),"+tex+".rgb);\n";
    }
  }else if(s.stage==Material20Stage::ambient) {
    out.fragment+="uniform vec4 ambient;\n";ps+="color=ambient;\n";
  }else if(s.stage==Material20Stage::light) {
    out.vertex+=R"(uniform vec3 light_position,eye_position;
uniform vec4 attenuation_parameters;
uniform int directional;
uniform vec4 albedo_projection;
out vec3 light_vector,halfway_vector,attenuation_uv;
out vec4 albedo_uv,vertex_color;
)";
    vs+=R"(vec3 B=cross(normal,tangent);
vec3 L=directional!=0 ? light_position : light_position-position;
light_vector=vec3(dot(L,normal),dot(L,B),dot(L,tangent));
vec3 H=normalize(L)+normalize(eye_position-position);
halfway_vector=vec3(dot(H,normal),dot(H,B),dot(H,tangent));
attenuation_uv=position*attenuation_parameters.w+attenuation_parameters.xyz;
albedo_uv=vec4(gl_Position.xy*albedo_projection.xy+gl_Position.ww*albedo_projection.zw,0.0,gl_Position.w);
vertex_color=color.bgra;
)";
    out.fragment+=R"(in vec3 light_vector,halfway_vector,attenuation_uv;
in vec4 albedo_uv,vertex_color;
uniform vec4 diffuse,specular;
)";
    const auto bumps=unsigned(bool(s.textures[4]))+unsigned(bool(s.textures[5]));
    ps+="vec4 normal_map="+std::string(bumps ? sample(s.textures[4] ? 4 : 5) : "vec4(1.0,0.0,0.0,0.0)")+";\nvec3 N=normal_map.rgb;\n";
    if(bumps==2)ps+="N+="+sample(5)+".rgb;\n";
    if(bumps==2 || (flags&0x20))ps+="N=normalize(N);\n";
    ps+="vec3 D=diffuse.rgb*clamp(dot(normalize(light_vector),N),0.0,1.0);\nvec3 S=vec3(0.0);\n";
    if(flags&1) {
      ps+="S=specular.rgb*pow(clamp(dot(normalize(halfway_vector),N),0.0,1.0),specular.a);\n";
      if(flags&0x80)ps+="S*=normal_map.a;\n";
    }
    ps+=R"(vec2 projected=albedo_uv.xy/albedo_uv.w;
vec3 albedo=texture(material_tex7,vec2(projected.x,1.0-projected.y)).rgb;
float a=clamp(dot(attenuation_uv,attenuation_uv),0.0,1.0);
color=vec4(clamp((albedo*D+S)*(1.0-a),0.0,1.0),a);
)";
    if(flags&8)ps+="color*=vertex_color;\n";
  }else {
    const auto env=s.compiled_parameters[18];
    out.vertex+="uniform mat4 environment_matrix;\nuniform vec3 eye_position;\nout vec3 env_normal,env_eye,env_bitangent,env_tangent;\n";
    vs+="env_normal=mat3(environment_matrix)*normal;\nenv_eye=mat3(environment_matrix)*(eye_position-position);\n";
    if(env&0x20000)vs+="env_bitangent=mat3(environment_matrix)*cross(normal,tangent);\nenv_tangent=mat3(environment_matrix)*tangent;\n";
    out.fragment+="in vec3 env_normal,env_eye,env_bitangent,env_tangent;\nuniform float environment_flip;\n";
    if(env&0x20000)ps+="vec4 normal_map="+sample(4)+";\nvec3 N=normal_map.x*env_normal+normal_map.y*env_bitangent+normal_map.z*env_tangent;\n";
    else ps+="vec3 N=env_normal;\n";
    if(env&0x10000)ps+="vec3 E=normalize(env_eye);N=normalize(N);N=E-2.0*dot(E,N)*N;\n";
    ps+="vec2 env_uv=normalize(N).xy*vec2(0.5,-0.5)+vec2(0.5);\nif(environment_flip>0.5)env_uv.y=1.0-env_uv.y;\ncolor=texture(material_tex6,env_uv);\n";
    if(flags&0x80)ps+="color.rgb*=normal_map.a;\n";
  }
  out.vertex+=vs+"}\n";out.fragment+=ps+"}\n";return out;
}
} // namespace wz3
