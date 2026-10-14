// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/print.hpp"
#include <cfenv>
#include <cmath>
namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Nearest {int previous=std::fegetround();Nearest(){std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetround(previous);}};
}
PrintLayout layout_print(std::span<const std::uint32_t> parameters,std::string_view text,const std::array<GlyphMetric,256>& glyphs,bool legacy_alignment) {
  const Nearest rounding;require(parameters.size()==5 || parameters.size()==6,"unsupported Print layout");
  std::array<std::uint32_t,6> p{};std::copy(parameters.begin(),parameters.end(),p.begin());
  // Both the five-word editor record and the six-word final 1.01 release
  // predate the newer two-bit alignment field. The release reference confirms
  // bit 0 centers each line. Later six-word records retain newer semantics.
  if((parameters.size()==5 || legacy_alignment) && (p[0]&1) && !(p[0]&0x300))p[0]=(p[0]&~1u)|0x100;
  const auto flags=p[0];require(!(flags&~0x377u),"unsupported Print flags");
  std::array<float,5> v;for(unsigned i=0;i<5;++i) {v[i]=as_float(p[i+1]);require(std::isfinite(v[i]) && std::abs(v[i])<=1e6f,"invalid Print numeric parameter");}
  const auto sx=v[0],sy=v[1],spacing=v[2],line_spacing=v[3],radius=v[4];
  require(text.size()<=1024*1024,"Print text limit");
  const auto width=[&](unsigned chr) {const auto& g=glyphs[chr];const float height=g.uv[3]-g.uv[1];return height>0 ? g.advance/height*sx : 0.f;};
  const auto line_width=[&](std::string_view line) {float w=0;for(unsigned char c:line)w+=width(c)+spacing;return w-spacing;};
  PrintLayout result;result.normalized=flags&4;float y=(flags&2) ? sy/2 : 0;
  std::size_t at=0;
  while(at<text.size() && text[at]) {
    std::string line;std::optional<std::size_t> marker;
    while(at<text.size() && text[at] && text[at]!='\n') {
      if(text[at]=='%' && at+1<text.size()) {
        const auto next=text[at+1];
        if(next=='|') {marker=line.size();at+=2;continue;}
        const bool number=next>='0' && next<='9' && at+3<text.size() && text[at+2] && text[at+3];
        require(!number && next!='i' && next!='s',"Print game/FPS substitutions are not ported");
      }
      require(line.size()<511,"Print line exceeds archived 512-byte buffer");line.push_back(text[at++]);
    }
    float x=0;
    switch((flags>>8)&3) {
    case 1:x-=marker ? line_width(std::string_view(line).substr(0,*marker)) : line_width(line)/2;break;
    case 2:x-=line_width(line);break;
    default:break;
    }
    for(unsigned char c:line) {
      const auto& g=glyphs[c];const float h=g.uv[3]-g.uv[1];
      if(result.vertices.size()<4096*4 && h>0) {
        float top=y,bottom=y-sy,ztop=0,zbottom=0;
        if(radius!=0) {const auto inverse=1/radius;top=float(std::sin(double(inverse*y)))*radius;ztop=-float(std::cos(double(inverse*y)))*radius;bottom=float(std::sin(double(inverse*(y-sy))))*radius;zbottom=-float(std::cos(double(inverse*(y-sy))))*radius;}
        const auto left=x+g.pre_space*sx/h,right=left+(g.uv[2]-g.uv[0])/h*sx;
        result.vertices.push_back({{left,top,ztop},{g.uv[0],g.uv[1]},0xffffffff});
        result.vertices.push_back({{right,top,ztop},{g.uv[2],g.uv[1]},0xffffffff});
        result.vertices.push_back({{right,bottom,zbottom},{g.uv[2],g.uv[3]},0xffffffff});
        result.vertices.push_back({{left,bottom,zbottom},{g.uv[0],g.uv[3]},0xffffffff});
      }
      x+=width(c)+spacing;
    }
    while(at<text.size() && text[at]=='\n') {y-=sy+line_spacing;++at;}
  }
  for(const auto& vertex:result.vertices)for(auto x:vertex.position)require(std::isfinite(x),"nonfinite Print vertex");
  return result;
}
SceneMatrix print_normalized_projection(unsigned width,unsigned height) {
  require(width && height,"invalid Print viewport");
  // Normalized sMaterialEnv, its (-1,+1) model translation, and D3D depth to GL.
  return {1,0,0,0,0,1,0,0,0,0,2.f/4096,0,-1.f-1.f/width,1.f+1.f/height,-1,1};
}
} // namespace wz3
