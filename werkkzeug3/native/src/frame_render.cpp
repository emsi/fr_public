// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/frame.hpp"
#include "wz3/texture.hpp"
#include "gles.hpp"
#include <algorithm>
#include <iostream>

namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
constexpr const char* quad_vertex=R"(#version 300 es
  uniform vec4 rectangle;
  uniform vec4 uv_rect[4];
  uniform mat3 uv_transform[4];
  uniform vec4 flip_texture;
  uniform float depth;
  out vec2 material_uv[4];
  out vec4 material_vertex_color;
  void main() {
    vec2 p=vec2(float((gl_VertexID>>1)&1),float(gl_VertexID&1));
    vec2 xy=mix(rectangle.xy,rectangle.zw,p);
    // The source's -half-pixel D3D projection compensates D3D9 pixel centers.
    // GLES centers are already at +0.5; normalized edges need no extra shift.
    gl_Position=vec4(xy.x*2.0-1.0,1.0-xy.y*2.0,depth*2.0-1.0,1.0);
    for(int i=0;i<4;++i) {
      material_uv[i]=(uv_transform[i]*vec3(mix(uv_rect[i].xy,uv_rect[i].zw,p),1.0)).xy;
      if(flip_texture[i]>0.5)material_uv[i].y=1.0-material_uv[i].y;
    }
    material_vertex_color=vec4(0.0);
  })";
void clear(FrameClear c) {
  glDepthMask(GL_TRUE);glClearDepthf(1);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glClearColor(((c.color>>16)&255)/255.0f,((c.color>>8)&255)/255.0f,(c.color&255)/255.0f,(c.color>>24)/255.0f);
  glClear(((c.flags&1) ? GL_COLOR_BUFFER_BIT : 0)|((c.flags&2) ? GL_DEPTH_BUFFER_BIT : 0));
}
}
struct FrameRenderer::Impl {
  const Project& project;const Graph& graph;gles::Context context{1,1};
  struct Surface {GLuint color{},depth{},fbo{};unsigned width{},height{};};
  std::map<std::size_t,Surface> surfaces;
  std::map<std::size_t,GLuint> bitmaps;
  std::map<std::string,GLuint> programs;
  GLuint vao{};std::array<GLuint,4> samplers{};
  std::size_t surface_bytes{},bitmap_bytes{};
  Impl(const Project& p,const Graph& g):project(p),graph(g) {
    std::cerr<<"GL vendor: "<<glGetString(GL_VENDOR)<<"\nGL renderer: "<<glGetString(GL_RENDERER)<<"\nGL version: "<<glGetString(GL_VERSION)<<'\n';
    glGenVertexArrays(1,&vao);glGenSamplers(4,samplers.data());
  }
  ~Impl() {
    eglMakeCurrent(context.display,context.surface,context.surface,context.context);
    for(const auto& [id,s]:surfaces) {static_cast<void>(id);destroy(s);}
    for(const auto& [id,t]:bitmaps) {static_cast<void>(id);glDeleteTextures(1,&t);}
    for(const auto& [source,p]:programs) {static_cast<void>(source);glDeleteProgram(p);}
    glDeleteSamplers(4,samplers.data());glDeleteVertexArrays(1,&vao);
  }
  void activate() {gles::egl_check(eglMakeCurrent(context.display,context.surface,context.surface,context.context),"frame context activation");}
  static void destroy(const Surface& s) {glDeleteTextures(1,&s.color);glDeleteRenderbuffers(1,&s.depth);glDeleteFramebuffers(1,&s.fbo);}
  void surface(std::size_t id,FrameSurface size) {
    require(size.width && size.height && size.width<=4096 && size.height<=4096,"invalid frame surface dimensions");
    auto& s=surfaces[id];if(s.width==size.width && s.height==size.height)return;
    surface_bytes-=std::size_t(s.width)*s.height*8;destroy(s);s={};
    const auto bytes=std::size_t(size.width)*size.height*8;require(bytes<=256u*1024*1024-surface_bytes,"frame render target budget exceeded");surface_bytes+=bytes;
    s.width=size.width;s.height=size.height;glGenTextures(1,&s.color);glBindTexture(GL_TEXTURE_2D,s.color);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,s.width,s.height);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    glGenRenderbuffers(1,&s.depth);glBindRenderbuffer(GL_RENDERBUFFER,s.depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,s.width,s.height);
    glGenFramebuffers(1,&s.fbo);glBindFramebuffer(GL_FRAMEBUFFER,s.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.color,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,s.depth);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"incomplete frame surface");
    // Original allocation contents are unspecified. Start deterministically;
    // subsequent frames/allocations retain contents until the graph clears them.
    glDisable(GL_SCISSOR_TEST);clear({3,0xff000000});
  }
  GLuint bitmap(std::size_t id) {
    if(auto found=bitmaps.find(id);found!=bitmaps.end())return found->second;
    const auto t=evaluate_texture(project,graph,id);const auto pixels=rgba8(*t);
    require(pixels.size()<=256u*1024*1024-bitmap_bytes,"frame bitmap budget exceeded");
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,t->width,t->height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glGenerateMipmap(GL_TEXTURE_2D);
    bitmap_bytes+=pixels.size();bitmaps.emplace(id,texture);return texture;
  }
  GLuint program(const MaterialShader& s) {
    const auto source=material_fragment_shader(s);if(auto found=programs.find(source);found!=programs.end())return found->second;
    require(programs.size()<4096,"frame shader cache limit");
    const auto vertex=gles::shader(GL_VERTEX_SHADER,quad_vertex);GLuint fragment;
    try {fragment=gles::shader(GL_FRAGMENT_SHADER,source.c_str());}catch(...) {glDeleteShader(vertex);throw;}
    const auto p=glCreateProgram();glAttachShader(p,vertex);glAttachShader(p,fragment);glLinkProgram(p);glDeleteShader(vertex);glDeleteShader(fragment);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("frame shader link failed: ")+log);}
    programs.emplace(source,p);return p;
  }
  void quad(const FrameQuad& q,std::size_t destination) {
    const auto& s=q.shader;const auto flags=s.words[0];
    require(!(flags&0xfc0078),"Layer2D fog, shadow or depth bias is not ported");
    const auto p=program(s);glUseProgram(p);glBindVertexArray(vao);
    std::array<float,36> matrices{};std::array<float,16> rects{},colors{};std::array<float,4> flip{};
    for(unsigned i=0;i<4;++i) {
      auto uv_shader=s;uv_shader.words[4+i]&=~0x10u; // Quad UV0 and UV1 are supplied explicitly.
      const auto m=material_uv_matrix(uv_shader,i);std::copy(m.begin(),m.end(),matrices.begin()+i*9);
      std::copy(q.uv[i].begin(),q.uv[i].end(),rects.begin()+i*4);
      for(unsigned c=0;c<4;++c)colors[i*4+c]=((s.words[28+i]>>(c==0 ? 16 : c==1 ? 8 : c==2 ? 0 : 24))&255)/255.0f;
      require(bool(q.textures[i])==bool(s.textures[i]),"frame shader texture binding mismatch");
      if(!q.textures[i])continue;
      const auto binding=*q.textures[i];GLuint texture;
      glActiveTexture(GL_TEXTURE0+i);
      if(binding.surface) {
        require(binding.id!=destination,"render target feedback is not supported");
        require(surfaces.contains(binding.id),"missing sampled frame surface");texture=surfaces.at(binding.id).color;flip[i]=1;
      } else texture=bitmap(binding.id);
      glBindTexture(GL_TEXTURE_2D,texture);const auto tf=s.words[4+i],filter=tf&7,sampler=samplers[i];
      constexpr GLint min_filter[]={GL_NEAREST,GL_LINEAR,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR,GL_NEAREST_MIPMAP_NEAREST,GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST,GL_NEAREST};
      glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,min_filter[filter]);glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,(filter==1 || filter==2 || filter==3 || filter==5) ? GL_LINEAR : GL_NEAREST);
      glSamplerParameteri(sampler,GL_TEXTURE_WRAP_S,(tf&0x100) ? GL_CLAMP_TO_EDGE : GL_REPEAT);glSamplerParameteri(sampler,GL_TEXTURE_WRAP_T,(tf&0x100) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
      const auto extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
      const bool anisotropic=extensions && std::string_view(extensions).find("GL_EXT_texture_filter_anisotropic")!=std::string_view::npos;
      require(filter!=3 || anisotropic,"material needs anisotropic filtering");
      if(anisotropic) {GLfloat maximum=1;glGetFloatv(0x84ff,&maximum);glSamplerParameterf(sampler,0x84fe,filter==3 ? std::min(4.0f,maximum) : 1.0f);}
      glBindSampler(i,sampler);glUniform1i(glGetUniformLocation(p,("material_tex"+std::to_string(i)).c_str()),GLint(i));
    }
    glUniform4fv(glGetUniformLocation(p,"rectangle"),1,q.rectangle.data());glUniform1f(glGetUniformLocation(p,"depth"),q.depth);
    glUniform4fv(glGetUniformLocation(p,"uv_rect[0]"),4,rects.data());glUniformMatrix3fv(glGetUniformLocation(p,"uv_transform[0]"),4,GL_FALSE,matrices.data());
    glUniform4fv(glGetUniformLocation(p,"flip_texture"),1,flip.data());glUniform4fv(glGetUniformLocation(p,"material_colors[0]"),4,colors.data());
    glFrontFace(GL_CCW);if(flags&2)glDisable(GL_CULL_FACE);else {glEnable(GL_CULL_FACE);glCullFace((flags&4) ? GL_FRONT : GL_BACK);}
    glEnable(GL_DEPTH_TEST);glDepthMask((flags&0x100) ? GL_TRUE : GL_FALSE);glDepthFunc(!(flags&0x200) ? GL_ALWAYS : (flags&0x80) ? GL_EQUAL : GL_LEQUAL);
    const auto blend=(flags>>12)&15;require(blend<=9,"unsupported frame blend mode");
    if(!blend)glDisable(GL_BLEND);
    else {
      constexpr GLenum src[]={0,GL_SRC_ALPHA,GL_ONE,GL_ZERO,GL_DST_COLOR,GL_ONE,GL_ONE,GL_ONE_MINUS_DST_COLOR,GL_DST_ALPHA,GL_SRC_ALPHA};
      constexpr GLenum dst[]={0,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_SRC_COLOR,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_ONE,GL_ONE_MINUS_SRC_COLOR,GL_ONE,GL_ONE};
      glEnable(GL_BLEND);glBlendEquation(blend==6 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);glBlendFunc(src[blend],dst[blend]);
    }
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  }
};
FrameRenderer::FrameRenderer(const Project& p,const Graph& g):impl(std::make_unique<Impl>(p,g)) {}
FrameRenderer::~FrameRenderer()=default;
std::vector<std::uint8_t> FrameRenderer::render(const DemoFrame& frame,bool diagnostic) {
  if(!diagnostic)for(const auto& c:frame.commands)if(std::holds_alternative<SceneFrame>(c.draw))
    throw std::runtime_error("scene material drawing is not ported; --diagnostic-geometry enables checker geometry explicitly");
  auto& s=*impl;s.activate();glDisable(GL_DITHER);
  require(frame.surfaces.contains(0) && frame.surfaces.at(0).width==frame.width && frame.surfaces.at(0).height==frame.height,"invalid screen surface");
  for(const auto& [id,size]:frame.surfaces)s.surface(id,size);
  for(const auto& c:frame.commands) {
    const auto& dst=s.surfaces.at(c.target.surface);
    require(c.target.width && c.target.height && c.target.width<=dst.width && c.target.height<=dst.height,"frame viewport outside target");
    glBindFramebuffer(GL_FRAMEBUFFER,dst.fbo);glViewport(0,dst.height-c.target.height,c.target.width,c.target.height);
    glEnable(GL_SCISSOR_TEST);glScissor(0,dst.height-c.target.height,c.target.width,c.target.height);
    try {
      if(const auto* q=std::get_if<FrameQuad>(&c.draw))s.quad(*q,c.target.surface);
      else if(const auto* cl=std::get_if<FrameClear>(&c.draw))clear(*cl);
      else {const auto& scene=std::get<SceneFrame>(c.draw);clear({scene.viewport_flags&3,scene.background});draw_scene_geometry(scene,c.target.width,c.target.height);}
      require(glGetError()==GL_NO_ERROR,"frame draw produced an OpenGL error");
    }catch(const std::exception& e) {throw std::runtime_error("operator "+std::to_string(c.operator_id)+": "+e.what());}
  }
  glBindFramebuffer(GL_FRAMEBUFFER,s.surfaces.at(0).fbo);std::vector<std::uint8_t> pixels(std::size_t(frame.width)*frame.height*4);
  glReadPixels(0,0,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());require(glGetError()==GL_NO_ERROR,"frame readback failed");
  for(unsigned y=0;y<frame.height/2;++y)std::swap_ranges(pixels.begin()+std::size_t(y)*frame.width*4,pixels.begin()+std::size_t(y+1)*frame.width*4,pixels.begin()+std::size_t(frame.height-y-1)*frame.width*4);
  return pixels;
}
} // namespace wz3
