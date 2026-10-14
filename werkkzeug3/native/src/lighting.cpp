// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/lighting.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
namespace wz3 {
namespace {
struct Nearest {int previous=std::fegetround();Nearest(){std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetround(previous);}};
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Vec3 cross(Vec3 a,Vec3 b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
float dot(Vec3 a,Vec3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec3 unit(Vec3 v){const auto d=dot(v,v);if(d<=1e-20f)return {1,0,0};const auto s=float(1/std::sqrt(double(d)));for(auto& c:v)c*=s;return v;}
}
LightingTables lighting_tables() {
  const Nearest rounding;LightingTables out;out.cube.reserve(6*64*64*4);out.attenuation.reserve(32*32*32*4);
  const Vec3 axes[6][2]={{{0,0,-1},{0,1,0}},{{0,0,1},{0,1,0}},{{1,0,0},{0,0,-1}},{{1,0,0},{0,0,1}},{{1,0,0},{0,1,0}},{{-1,0,0},{0,1,0}}};
  for(const auto& face:axes)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
    auto v=cross(face[0],face[1]);for(unsigned c=0;c<3;++c){v[c]*=31.5f;v[c]+=face[0][c]*(float(x)-31.5f);v[c]+=face[1][c]*(31.5f-float(y));}v=unit(v);
    for(auto c:v)out.cube.push_back(std::uint8_t(std::nearbyint(128.f+c*127.f)));
    out.cube.push_back(0);
  }
  const auto scale=2.f/30.f;
  for(unsigned z=0;z<32;++z)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
    const Vec3 v={(float(x)-16)*scale,(float(y)-16)*scale,(float(z)-16)*scale};
    const auto a=std::uint8_t(std::nearbyint(std::max(1.f-dot(v,v),0.f)*255.f));
    for(unsigned c=0;c<4;++c)out.attenuation.push_back(a);
  }
  return out;
}
std::vector<SignedNormalLevel> signed_normal_levels(const Texture& texture) {
  const Nearest rounding;require(texture.format==5,"signed normal upload needs Q8W8V8U8 format");
  unsigned width=texture.width,height=texture.height;
  require(width && height && width<=4096 && height<=4096 && !(width&(width-1)) && !(height&(height-1)) && texture.pixels.size()==std::size_t(width)*height,"invalid signed normal texture");
  auto source=texture.pixels;std::vector<SignedNormalLevel> levels;
  for(unsigned level=0;;++level) {
    SignedNormalLevel output{width,height,{}};output.pixels.reserve(std::size_t(width)*height*4);
    for(const auto& p:source) {
      const auto n=unit({float(int(p[0])-0x4000),float(int(p[1])-0x4000),float(int(p[2])-0x4000)});
      for(auto c:n)output.pixels.push_back(std::int8_t(int(c*127.f)));
      const int a=(int(p[3])-0x4000)>>7;output.pixels.push_back(std::bit_cast<std::int8_t>(std::uint8_t(a&255)));
    }
    levels.push_back(std::move(output));if(width<=1 || height<=1 || (texture.mip_count && level+1>=texture.mip_count))break;
    bool filter=(texture.mip_threshold&15)<=level;if(texture.mip_threshold&16)filter=!filter;
    std::vector<Pixel> next(std::size_t(width/2)*(height/2));
    for(unsigned y=0;y<height/2;++y)for(unsigned x=0;x<width/2;++x)for(unsigned c=0;c<4;++c) {
      const auto i=std::size_t(2*y)*width+2*x;
      next[std::size_t(y)*(width/2)+x][c]=filter ? (unsigned(source[i][c])+source[i+1][c]+source[i+width][c]+source[i+width+1][c]+2)>>2 : source[i][c];
    }
    source=std::move(next);width/=2;height/=2;
  }
  return levels;
}
Vec3 light_model_vector(const SceneMatrix& model,const Vec3& vector,bool point) {
  Vec3 v=vector;if(point)for(unsigned c=0;c<3;++c)v[c]-=model[12+c];
  Vec3 out{};for(unsigned c=0;c<3;++c)out[c]=model[c*4]*v[0]+model[c*4+1]*v[1]+model[c*4+2]*v[2];return out;
}
std::array<float,4> light_color(const MaterialShader& s,const SceneLight& light) {
  std::array<float,4> result;require(std::isfinite(light.amplify),"invalid light amplification");
  for(unsigned c=0;c<4;++c){const auto shift=c==0 ? 16 : c==1 ? 8 : c==2 ? 0 : 24;result[c]=float((s.words[28]>>shift)&255)/255.f*(float((light.color>>shift)&255)/255.f)*light.amplify;}
  return result;
}
std::array<float,2> light_specular_parameters(float power) {
  require(std::isfinite(power),"invalid specular power");const auto inverse=1.f/7.336032f;
  const auto z=inverse+(std::clamp(power,8.f,60.f)-8.f)*.01565302f;return {z,inverse-z};
}
std::string material_light_fragment_shader(const MaterialShader& s) {
  require((s.words[2]&7)==5,"unsupported lighting mode");require(!(s.words[1]&~0xc0u),"unsupported light shader special flags");
  require(!s.textures[0] && !s.textures[2] && !s.textures[3],"light shader only supports its bump-map slot");
  const bool specular=!(s.words[1]&0x40);
  std::string code=R"(#version 300 es
precision highp float;
in vec2 material_uv[4];
in vec3 light_vector;
in vec3 halfway_vector;
in vec3 attenuation_uv;
uniform highp samplerCube normal_cube;
uniform highp sampler3D attenuation_volume;
uniform vec4 light_color;
uniform vec2 specular_parameters;
out vec4 color;
)";
  if(s.textures[1])code+="uniform highp sampler2D material_tex1;\n";
  code+="void main(){\nvec4 normal_value="+std::string(s.textures[1] ? "texture(material_tex1,material_uv[1])" : "vec4(1.0,0.0,0.0,0.5)")+";\n";
  code+="vec3 L=texture(normal_cube,light_vector).rgb*2.0-1.0;\nfloat attenuation=texture(attenuation_volume,attenuation_uv).r;\nfloat diffuse=clamp(dot(L,normal_value.rgb),0.0,1.0);\nfloat specular=0.0;\n";
  if(specular) {
    code+="vec3 H=texture(normal_cube,halfway_vector).rgb*2.0-1.0;\nfloat h=clamp(dot(H,normal_value.rgb),0.0,1.0);\nfloat p=clamp((h*specular_parameters.x+specular_parameters.y)*4.0,0.0,1.0);\np=p*p*2.0;\np=p*p*2.0;\np=clamp(p*p*2.0,0.0,1.0);\nspecular=p*light_color.a*attenuation;\n";
    if(s.words[1]&0x80)code+=s.textures[1] ? "specular*=normal_value.a;\n" : "specular=0.0;\n";
  }
  code+="color=clamp(vec4(diffuse*light_color.rgb*attenuation,specular),0.0,1.0);\n";
  if(s.words[0]&1)code+="if(color.a<=128.0/255.0)discard;\n";
  return code+"}\n";
}
} // namespace wz3
