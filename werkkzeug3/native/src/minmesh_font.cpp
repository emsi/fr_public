// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "minmesh_font.hpp"
#include <GL/glu.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <cfenv>
#include <exception>
namespace wz3::legacy::minimal {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void ft_check(FT_Error error,const char* action){if(error)throw std::runtime_error(std::string(action)+" failed: "+std::to_string(error));}
struct Nearest {
  std::fenv_t saved;
  Nearest(){require(std::fegetenv(&saved)==0 && std::fesetround(FE_TONEAREST)==0,"cannot set Font3D rounding");}
  ~Nearest(){std::fesetenv(&saved);}
};
float checked_height(float height){require(std::isfinite(height) && height<=512,"invalid Font3D height");return std::max(height,8/128.f);}
unsigned codepoint(unsigned char code) {
  constexpr unsigned cp1252[]={0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
    0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
  return code>=128 && code<160 ? cp1252[code-128] : code;
}
struct Font {
  FT_Library library{};FT_Face face{};
  explicit Font(const std::filesystem::path& path) {
    ft_check(FT_Init_FreeType(&library),"FT_Init_FreeType");
    try {ft_check(FT_New_Face(library,path.c_str(),0,&face),"FT_New_Face");
      require(FT_IS_SCALABLE(face) && FT_IS_SFNT(face),"Font3D requires scalable TrueType outlines");
      ft_check(FT_Select_Charmap(face,FT_ENCODING_UNICODE),"FT_Select_Charmap");
    }catch(...){if(face)FT_Done_Face(face);FT_Done_FreeType(library);throw;}
  }
  ~Font(){FT_Done_Face(face);FT_Done_FreeType(library);}
  Font(const Font&)=delete;
  Font& operator=(const Font&)=delete;
};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "font3d_kernel.inc"
#pragma GCC diagnostic pop
struct TessState {
  GenMinMesh* mesh;std::exception_ptr failure;GLenum error{};
  template<class F> void call(F action)noexcept{if(!failure)try{action();}catch(...){failure=std::current_exception();}}
  void check(){if(failure)std::rethrow_exception(failure);if(error)throw std::runtime_error("Font3D triangulation failed: "+std::to_string(error));}
};
void begin(GLenum type,void* data)noexcept {auto& s=*static_cast<TessState*>(data);s.call([&]{require(type==GL_TRIANGLES,"Font3D needs individual triangles");font3DBeginCB(type,s.mesh);});}
void vertex(void* index,void* data)noexcept {auto& s=*static_cast<TessState*>(data);s.call([&]{font3DVertexCB(index,s.mesh);});}
void combine(GLdouble coords[3],void* inputs[4],GLfloat weights[4],void** output,void* data)noexcept {
  *output=nullptr;auto& s=*static_cast<TessState*>(data);s.call([&]{font3DCombineCB(coords,inputs,weights,output,s.mesh);});
}
void edge(GLboolean flag,void* data)noexcept {font3DEdgeFlagCB(flag,static_cast<TessState*>(data)->mesh);}
void error(GLenum value,void* data)noexcept {static_cast<TessState*>(data)->error=value;}
bool same(const GenMinVector& a,const GenMinVector& b){return a.x==b.x && a.y==b.y && a.z==b.z;}
}
std::vector<Font3DGlyph> font3d_outlines(const std::filesystem::path& path,std::string_view text,float height) {
  const Nearest rounding;height=checked_height(height);
  require(!path.empty(),"Font3D needs an explicit font file");require(text.size()<=16383 && text.find('\0')==text.npos,"invalid Font3D text");
  Font font(path);const auto* os2=static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(font.face,ft_sfnt_os2));
  // Positive CreateFont height describes the cell, rather than the em square.
  const long cell=os2 && os2->version!=0xffff ? long(os2->usWinAscent)+os2->usWinDescent : long(font.face->ascender)-font.face->descender;
  require(cell>0,"Font3D font has invalid cell metrics");const long pixels=long(height*128);long pen=0;std::size_t count=0;
  std::vector<Font3DGlyph> glyphs;glyphs.reserve(text.size());
  for(unsigned char ch:text) {
    ft_check(FT_Load_Char(font.face,codepoint(ch),FT_LOAD_NO_SCALE|FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP|FT_LOAD_IGNORE_TRANSFORM),"FT_Load_Char");
    require(font.face->glyph->format==FT_GLYPH_FORMAT_OUTLINE,"Font3D glyph has no outline");const auto& outline=font.face->glyph->outline;
    require(outline.n_points>=0 && std::size_t(outline.n_points)<=kernel_limit-count,"Font3D outline budget exceeded");count+=outline.n_points;
    const auto point=[&](int i) {
      const auto& p=outline.points[i];const auto x=FT_MulDiv(p.x,pixels*65536,cell),y=FT_MulDiv(p.y,pixels*65536,cell);
      require(std::abs(double(x))<=2147483647 && std::abs(double(y))<=2147483647,"Font3D outline coordinate overflow");
      return GenMinVector{(float(pen)+float(x)/65536.f)/128.f,float(y)/(65536.f*128.f),0};
    };
    const auto tag=[&](int i){const auto value=FT_CURVE_TAG(outline.tags[i]);require(value==FT_CURVE_TAG_ON || value==FT_CURVE_TAG_CONIC,"Font3D requires quadratic TrueType curves");return value;};
    Font3DGlyph glyph;int first=0;
    for(int c=0;c<outline.n_contours;++c) {
      int last=outline.contours[c];require(first<=last && last<outline.n_points,"invalid Font3D contour");const int next=last+1;Font3DContour contour;
      if(tag(first)==FT_CURVE_TAG_ON)contour.start=point(first++);
      else if(tag(last)==FT_CURVE_TAG_ON)contour.start=point(last--);
      else {const auto a=point(first),b=point(last);contour.start.Lin3(a,b,.5f);}
      for(int i=first;i<=last;) {
        const auto p=point(i);
        if(tag(i)==FT_CURVE_TAG_ON){contour.segments.push_back({false,{},p});++i;}
        else if(i==last){contour.segments.push_back({true,p,contour.start});++i;}
        else if(tag(i+1)==FT_CURVE_TAG_ON){contour.segments.push_back({true,p,point(i+1)});i+=2;}
        else {GenMinVector mid;mid.Lin3(p,point(i+1),.5f);contour.segments.push_back({true,p,mid});++i;}
      }
      if(!contour.segments.empty() && !same(contour.segments.back().end,contour.start))contour.segments.push_back({false,{},contour.start});
      glyph.push_back(std::move(contour));first=next;
    }
    glyphs.push_back(std::move(glyph));pen+=FT_MulDiv(font.face->glyph->metrics.horiAdvance,pixels,cell);
    require(std::abs(double(pen))<=1000000,"Font3D text width limit");
  }
  return glyphs;
}
std::shared_ptr<GenMinMesh> font3d_paths(std::span<const Font3DGlyph> glyphs,float height,float extrude,float max_error) {
  const Nearest rounding;height=checked_height(height);
  require(std::isfinite(extrude) && std::abs(extrude)<=1000000 && std::isfinite(max_error) && max_error>=0 && max_error<=1000000,"invalid Font3D extrusion/tolerance");
  float tolerance=height*.1f*max_error;tolerance*=tolerance;
  auto mesh=std::make_shared<GenMinMesh>();TessState state{mesh.get(),{},0};
  std::unique_ptr<GLUtesselator,decltype(&gluDeleteTess)> tess(gluNewTess(),gluDeleteTess);require(bool(tess),"cannot create Font3D triangulator");
  gluTessNormal(tess.get(),0,0,-1);
  gluTessCallback(tess.get(),GLU_TESS_BEGIN_DATA,reinterpret_cast<_GLUfuncptr>(begin));
  gluTessCallback(tess.get(),GLU_TESS_VERTEX_DATA,reinterpret_cast<_GLUfuncptr>(vertex));
  gluTessCallback(tess.get(),GLU_TESS_COMBINE_DATA,reinterpret_cast<_GLUfuncptr>(combine));
  gluTessCallback(tess.get(),GLU_TESS_EDGE_FLAG_DATA,reinterpret_cast<_GLUfuncptr>(edge));
  gluTessCallback(tess.get(),GLU_TESS_ERROR_DATA,reinterpret_cast<_GLUfuncptr>(error));
  const auto valid=[](const GenMinVector& p){require(std::isfinite(p.x) && std::isfinite(p.y) && p.z==0,"invalid Font3D planar point");};
  for(const auto& glyph:glyphs) {
    if(glyph.empty())continue;
    gluTessBeginPolygon(tess.get(),&state);
    for(const auto& contour:glyph) {
      valid(contour.start);gluTessBeginContour(tess.get());font3DAddPoint(tess.get(),mesh.get(),contour.start);
      for(const auto& segment:contour.segments){valid(segment.end);if(segment.quadratic){valid(segment.control);font3DAddCurve(tess.get(),mesh.get(),segment.control,segment.end,tolerance);}else font3DAddPoint(tess.get(),mesh.get(),segment.end);state.check();}
      gluTessEndContour(tess.get());state.check();
    }
    gluTessEndPolygon(tess.get());state.check();
  }
  if(mesh->Faces.Count){require(mesh->Faces[mesh->Faces.Count-1].Count==0,"Font3D incomplete triangle");--mesh->Faces.Count;}
  tess.reset();font3DFinish(mesh.get(),extrude);mesh->validate();return mesh;
}
std::shared_ptr<GenMinMesh> font3d_mesh(const std::filesystem::path& path,std::string_view text,float height,float extrude,float max_error) {
  return font3d_paths(font3d_outlines(path,text,height),height,extrude,max_error);
}
}
