// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/frame.hpp"
#include "wz3/texture.hpp"
#include "wz3/print.hpp"
#include "wz3/scene_draw.hpp"
#include "wz3/lighting.hpp"
#include "wz3/fog.hpp"
#include <cmath>
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
constexpr const char* particle_vertex=R"(#version 300 es
  layout(location=0) in vec3 position;
  layout(location=1) in vec2 uv;
  layout(location=2) in vec4 color;
  uniform mat4 view_projection;
  uniform vec4 fog_plane;
  out float material_fog;
  uniform mat3 uv_transform[4];
  uniform vec4 flip_texture;
  out vec2 material_uv[4];
  out vec4 material_vertex_color;
  void main() {
    gl_Position=view_projection*vec4(position,1.0);
    material_fog=clamp(dot(vec4(position,1.0),fog_plane),0.0,1.0);
    for(int i=0;i<4;++i) {
      material_uv[i]=(uv_transform[i]*vec3(uv,1.0)).xy;
      if(flip_texture[i]>0.5)material_uv[i].y=1.0-material_uv[i].y;
    }
    material_vertex_color=color.bgra;
  })";
constexpr const char* light_vertex=R"(#version 300 es
  layout(location=0) in vec3 position;
  layout(location=1) in vec2 uv;
  layout(location=3) in vec3 normal;
  layout(location=4) in vec3 tangent;
  uniform mat4 view_projection;
  uniform vec4 fog_plane;
  out float material_fog;
  uniform mat3 uv_transform[4];
  uniform vec4 flip_texture;
  uniform vec3 light_position;
  uniform vec3 eye_position;
  uniform int directional;
  uniform vec4 attenuation_parameters;
  out vec2 material_uv[4];
  out vec3 light_vector;
  out vec3 halfway_vector;
  out vec3 attenuation_uv;
  void main() {
    gl_Position=view_projection*vec4(position,1.0);
    material_fog=clamp(dot(vec4(position,1.0),fog_plane),0.0,1.0);
    for(int i=0;i<4;++i){material_uv[i]=(uv_transform[i]*vec3(uv,1.0)).xy;if(flip_texture[i]>0.5)material_uv[i].y=1.0-material_uv[i].y;}
    vec3 B=cross(normal,tangent);
    vec3 L=directional!=0 ? light_position : light_position-position;
    light_vector=vec3(dot(L,normal),dot(L,B),dot(L,tangent));
    vec3 H=normalize(L)+normalize(eye_position-position);
    halfway_vector=vec3(dot(H,normal),dot(H,B),dot(H,tangent));
    attenuation_uv=position*attenuation_parameters.w+attenuation_parameters.xyz;
  })";
constexpr const char* shadow_vertex=R"(#version 300 es
  layout(location=0) in vec4 position;
  uniform mat4 view_projection;
  uniform vec3 light_position;
  void main() {
    // material11.vsh: finite vertex w=1; extruded direction w=0.
    gl_Position=view_projection*vec4(position.xyz+light_position*(position.w-1.0),position.w);
  })";
void clear(FrameClear c,bool stencil=false) {
  glDepthMask(GL_TRUE);glClearDepthf(1);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glClearColor(((c.color>>16)&255)/255.0f,((c.color>>8)&255)/255.0f,(c.color&255)/255.0f,(c.color>>24)/255.0f);
  if(stencil && (c.flags&2)) {glStencilMask(255);glClearStencil(0);}
  glClear(((c.flags&1) ? GL_COLOR_BUFFER_BIT : 0)|((c.flags&2) ? GL_DEPTH_BUFFER_BIT|(stencil ? GL_STENCIL_BUFFER_BIT : 0) : 0));
}
}
struct FrameRenderer::Impl {
  const Project& project;const Graph& graph;TextureOptions texture_options;std::unique_ptr<gles::Context> context;
  struct Surface {GLuint color{},depth{},fbo{};unsigned width{},height{};};
  std::map<std::size_t,Surface> surfaces;
  Surface shared_depth;
  std::map<std::size_t,GLuint> bitmaps;
  std::map<std::size_t,std::shared_ptr<const Texture>> supplied_bitmaps;
  std::map<std::size_t,std::size_t> bitmap_sizes;
  std::map<std::string,GLuint> programs;
  struct MeshGeometry {
    std::shared_ptr<const Mesh> owner;GLuint buffer{};
    std::vector<std::pair<GLint,GLsizei>> batches;
  };
  struct MeshVertex {SceneEffectVertex vertex;Vec3 normal,tangent;};
  struct MeshDraw {GLuint buffer;GLint first;GLsizei count;};
  struct LightDraw {Vec3 position,eye;std::array<float,4> attenuation,color;std::array<float,2> specular;bool directional;};
  struct ShadowGeometry {
    std::shared_ptr<const Mesh> owner;std::vector<std::uint8_t> mask;
    ShadowMesh mesh;GLuint buffer{};std::size_t bytes{};
  };
  std::map<const Mesh*,ShadowGeometry> shadows;
  GLuint shadow_program{},shadow_index_buffer{};
  GLuint normal_cube{},attenuation_volume{};
  std::map<const Mesh*,MeshGeometry> geometry;
  std::size_t geometry_bytes{};
  GLuint vao{},particle_buffer{};std::array<GLuint,4> samplers{};
  std::size_t surface_bytes{},bitmap_bytes{};
  FrameRenderer::Stats stats;
  Impl(const Project& p,const Graph& g,const TextureOptions& options,FrameRenderer::Context mode):project(p),graph(g),texture_options(options) {
    if(mode==FrameRenderer::Context::Headless)context=std::make_unique<gles::Context>(1,1);
    require(glGetString(GL_VERSION)!=nullptr,"FrameRenderer needs a current GLES3 context");
    std::cerr<<"GL vendor: "<<glGetString(GL_VENDOR)<<"\nGL renderer: "<<glGetString(GL_RENDERER)<<"\nGL version: "<<glGetString(GL_VERSION)<<'\n';
    glGenBuffers(1,&particle_buffer);glGenVertexArrays(1,&vao);glGenSamplers(4,samplers.data());
    glGenBuffers(1,&shadow_index_buffer);
  }
  ~Impl() {
    if(context)eglMakeCurrent(context->display,context->surface,context->surface,context->context);
    for(const auto& [id,s]:surfaces) {static_cast<void>(id);destroy(s);}
    destroy(shared_depth);
    for(const auto& [id,t]:bitmaps) {static_cast<void>(id);glDeleteTextures(1,&t);}
    for(const auto& [source,p]:programs) {static_cast<void>(source);glDeleteProgram(p);}
    for(const auto& [id,g]:geometry) {static_cast<void>(id);glDeleteBuffers(1,&g.buffer);}
    for(const auto& [id,g]:shadows) {static_cast<void>(id);glDeleteBuffers(1,&g.buffer);}
    glDeleteProgram(shadow_program);glDeleteBuffers(1,&shadow_index_buffer);
    glDeleteTextures(1,&normal_cube);glDeleteTextures(1,&attenuation_volume);
    glDeleteBuffers(1,&particle_buffer);glDeleteSamplers(4,samplers.data());glDeleteVertexArrays(1,&vao);
  }
  void activate() {if(context)gles::egl_check(eglMakeCurrent(context->display,context->surface,context->surface,context->context),"frame context activation");}
  static void destroy(const Surface& s) {glDeleteTextures(1,&s.color);glDeleteRenderbuffers(1,&s.depth);glDeleteFramebuffers(1,&s.fbo);}
  void surface(std::size_t id,FrameSurface size) {
    require(size.width && size.height && size.width<=4096 && size.height<=4096,"invalid frame surface dimensions");
    auto& s=surfaces[id];if(s.width==size.width && s.height==size.height)return;
    surface_bytes-=std::size_t(s.width)*s.height*8;destroy(s);s={};
    const auto bytes=std::size_t(size.width)*size.height*8;require(bytes<=256u*1024*1024-surface_bytes,"frame render target budget exceeded");surface_bytes+=bytes;
    s.width=size.width;s.height=size.height;glGenTextures(1,&s.color);glBindTexture(GL_TEXTURE_2D,s.color);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,s.width,s.height);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    glGenRenderbuffers(1,&s.depth);glBindRenderbuffer(GL_RENDERBUFFER,s.depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,s.width,s.height);
    glGenFramebuffers(1,&s.fbo);glBindFramebuffer(GL_FRAMEBUFFER,s.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.color,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,s.depth);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"incomplete frame surface");
    ++stats.surface_allocations;
    // Original allocation contents are unspecified. Start deterministically;
    // subsequent frames/allocations retain contents until the graph clears them.
    glDisable(GL_SCISSOR_TEST);clear({3,0xff000000});glClearStencil(0);glStencilMask(255);glClear(GL_STENCIL_BUFFER_BIT);
  }
  void begin_release_frame(const DemoFrame& frame) {
    // The released D3D player clears the screen and its shared Z/stencil
    // surface before executing the graph (8246e1..824702). IPP color history
    // survives, but old depth must not freeze Viewports whose clear flags are 0.
    unsigned width=shared_depth.width,height=shared_depth.height;
    for(const auto& [id,s]:surfaces) {static_cast<void>(id);width=std::max(width,s.width);height=std::max(height,s.height);}
    if(width!=shared_depth.width || height!=shared_depth.height) {
      surface_bytes-=std::size_t(shared_depth.width)*shared_depth.height*4;destroy(shared_depth);shared_depth={};
      const auto bytes=std::size_t(width)*height*4;require(bytes<=256u*1024*1024-surface_bytes,"shared depth target budget exceeded");surface_bytes+=bytes;
      shared_depth.width=width;shared_depth.height=height;
      glGenRenderbuffers(1,&shared_depth.depth);glBindRenderbuffer(GL_RENDERBUFFER,shared_depth.depth);
      glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,width,height);
      glGenFramebuffers(1,&shared_depth.fbo);glBindFramebuffer(GL_FRAMEBUFFER,shared_depth.fbo);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,shared_depth.depth);
      const GLenum none=GL_NONE;glDrawBuffers(1,&none);glReadBuffer(GL_NONE);
      require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"incomplete shared depth surface");
      glDisable(GL_SCISSOR_TEST);clear({2,0},true);++stats.surface_allocations;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,shared_depth.fbo);glEnable(GL_SCISSOR_TEST);
    glScissor(0,shared_depth.height-frame.height,frame.width,frame.height);clear({2,0},true);
    glBindFramebuffer(GL_FRAMEBUFFER,surfaces.at(0).fbo);
    glScissor(0,0,frame.width,frame.height);clear({1,0});
  }
  static void copy_depth(const Surface& from,const Surface& to) {
    // D3D targets share depth at their top-left corner. GLES attachments have
    // different heights and bottom-left origins, so attaching one renderbuffer
    // everywhere would misalign them. Transfer the unscaled overlapping region.
    const auto w=std::min(from.width,to.width),h=std::min(from.height,to.height);
    glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,from.fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,to.fbo);
    glBlitFramebuffer(0,from.height-h,w,from.height,0,to.height-h,w,to.height,
                     GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,to.fbo);
  }
  GLuint bitmap(std::size_t id,std::shared_ptr<const Texture> supplied={}) {
    const auto found=bitmaps.find(id);
    if(found!=bitmaps.end() && (!supplied || (supplied_bitmaps.contains(id) && supplied_bitmaps.at(id)==supplied)))return found->second;
    const auto t=supplied ? supplied : evaluate_texture(project,graph,id,texture_options);
    const auto previous=bitmap_sizes.contains(id) ? bitmap_sizes.at(id) : 0;
    const auto levels=t->format==5 ? signed_normal_levels(*t) : std::vector<SignedNormalLevel>{};
    const auto pixels=t->format==5 ? std::vector<std::uint8_t>{} : rgba8(*t);
    std::size_t bytes=pixels.size();for(const auto& level:levels)bytes+=level.pixels.size();
    require(bytes<=256u*1024*1024-(bitmap_bytes-previous),"frame bitmap budget exceeded");
    GLuint texture=0;if(found!=bitmaps.end())texture=found->second;else glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    if(levels.empty()){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,t->width,t->height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,1000);glGenerateMipmap(GL_TEXTURE_2D);}
    else {for(std::size_t i=0;i<levels.size();++i){const auto& level=levels[i];glTexImage2D(GL_TEXTURE_2D,GLint(i),GL_RGBA8_SNORM,level.width,level.height,0,GL_RGBA,GL_BYTE,level.pixels.data());}glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,GLint(levels.size()-1));}
    bitmap_bytes=bitmap_bytes-previous+bytes;bitmap_sizes[id]=bytes;bitmaps[id]=texture;if(supplied)supplied_bitmaps[id]=std::move(supplied);++stats.bitmap_uploads;return texture;
  }
  void light_tables(GLuint p) {
    if(!normal_cube) {
      ++stats.lighting_uploads;
      const auto tables=lighting_tables();glGenTextures(1,&normal_cube);glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_CUBE_MAP,normal_cube);
      for(unsigned f=0;f<6;++f)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,tables.cube.data()+f*64*64*4);
      glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
      glGenTextures(1,&attenuation_volume);glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_3D,attenuation_volume);glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,32,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,tables.attenuation.data());
      glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
    }
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_CUBE_MAP,normal_cube);glBindSampler(4,0);glUniform1i(glGetUniformLocation(p,"normal_cube"),4);
    glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_3D,attenuation_volume);glBindSampler(5,0);glUniform1i(glGetUniformLocation(p,"attenuation_volume"),5);
  }
  GLuint program(const MaterialShader& s,bool particle) {
    const bool lit=(s.words[2]&7)!=0;require(!lit || particle,"lighting needs mesh vertex data");
    auto source=lit ? material_light_fragment_shader(s) : material_fragment_shader(s);if(s.words[0]&0x40)source=fog_fragment_shader(std::move(source));
    const auto key=(lit ? "light:" : particle ? "particle:" : "quad:")+source;if(auto found=programs.find(key);found!=programs.end())return found->second;
    require(programs.size()<4096,"frame shader cache limit");
    const auto vertex=gles::shader(GL_VERTEX_SHADER,lit ? light_vertex : particle ? particle_vertex : quad_vertex);GLuint fragment;
    try {fragment=gles::shader(GL_FRAGMENT_SHADER,source.c_str());}catch(...) {glDeleteShader(vertex);throw;}
    const auto p=glCreateProgram();glAttachShader(p,vertex);glAttachShader(p,fragment);glLinkProgram(p);glDeleteShader(vertex);glDeleteShader(fragment);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("frame shader link failed: ")+log);}
    programs.emplace(key,p);++stats.program_compilations;return p;
  }
  void quad(const FrameQuad& q,std::size_t destination,const SceneEffectDraw* effect=nullptr,const SceneMatrix* projection=nullptr,const MeshDraw* mesh=nullptr,const LightDraw* light=nullptr,const SceneFog* fog=nullptr) {
    const auto& s=q.shader;const auto flags=s.words[0];
    require(!(flags&(light ? 0xcc0018u : 0xfc0018u)),"draw shadow state or depth bias is not ported");
    require(!(flags&0x700000) || (light && (flags&0x700000)==0x300000),"unsupported stencil material operation");
    const bool vertices=effect || mesh;require(!(s.words[2]&7) || light,"missing scene light uniforms");require(!(flags&0x40) || (vertices && fog),"fog requires a scene camera");const auto p=program(s,vertices);glUseProgram(p);glBindVertexArray(vao);
    std::array<float,36> matrices{};std::array<float,16> rects{},colors{};std::array<float,4> flip{};
    for(unsigned i=0;i<4;++i) {
      auto uv_shader=s;if(!vertices)uv_shader.words[4+i]&=~0x10u; // Quad UV0 and UV1 are supplied explicitly.
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
      } else texture=bitmap(binding.id,(effect && effect->textures.contains(binding.id)) ? effect->textures.at(binding.id) : nullptr);
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
    if(light) {
      light_tables(p);glUniform3fv(glGetUniformLocation(p,"light_position"),1,light->position.data());glUniform3fv(glGetUniformLocation(p,"eye_position"),1,light->eye.data());
      glUniform1i(glGetUniformLocation(p,"directional"),light->directional);glUniform4fv(glGetUniformLocation(p,"attenuation_parameters"),1,light->attenuation.data());glUniform4fv(glGetUniformLocation(p,"light_color"),1,light->color.data());glUniform2fv(glGetUniformLocation(p,"specular_parameters"),1,light->specular.data());
    }
    if(fog){glUniform4fv(glGetUniformLocation(p,"fog_plane"),1,fog->plane.data());glUniform3fv(glGetUniformLocation(p,"fog_color"),1,fog->color.data());}
    if((flags&0x700000)==0x300000){glEnable(GL_STENCIL_TEST);glStencilFunc(GL_EQUAL,0,255);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);}else glDisable(GL_STENCIL_TEST);
    glColorMask(!(flags&0x20),!(flags&0x20),!(flags&0x20),!(flags&0x20));
    glFrontFace(vertices ? GL_CW : GL_CCW);if(flags&2)glDisable(GL_CULL_FACE);else {glEnable(GL_CULL_FACE);glCullFace((flags&4) ? GL_FRONT : GL_BACK);}
    glEnable(GL_DEPTH_TEST);glDepthMask((flags&0x100) ? GL_TRUE : GL_FALSE);glDepthFunc(!(flags&0x200) ? GL_ALWAYS : (flags&0x80) ? GL_EQUAL : GL_LEQUAL);
    const auto blend=(flags>>12)&15;require(blend<=9,"unsupported frame blend mode");
    if(!blend)glDisable(GL_BLEND);
    else {
      constexpr GLenum src[]={0,GL_SRC_ALPHA,GL_ONE,GL_ZERO,GL_DST_COLOR,GL_ONE,GL_ONE,GL_ONE_MINUS_DST_COLOR,GL_DST_ALPHA,GL_SRC_ALPHA};
      constexpr GLenum dst[]={0,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_SRC_COLOR,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_ONE,GL_ONE_MINUS_SRC_COLOR,GL_ONE,GL_ONE};
      glEnable(GL_BLEND);glBlendEquation(blend==6 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);glBlendFunc(src[blend],dst[blend]);
    }
    if(!vertices)glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    else {
      require(projection,"missing mesh/effect projection");
      GLint first=0;GLsizei count=0;
      if(mesh) {glBindBuffer(GL_ARRAY_BUFFER,mesh->buffer);first=mesh->first;count=mesh->count;}
      else {
        require(effect->vertices.size()%4==0 && effect->vertices.size()<=65536*4,"invalid effect geometry");
        std::vector<SceneEffectVertex> triangles;triangles.reserve(effect->vertices.size()/4*6);
        for(std::size_t base=0;base<effect->vertices.size();base+=4)for(unsigned i:{0u,1u,2u,0u,2u,3u})triangles.push_back(effect->vertices[base+i]);
        glBindBuffer(GL_ARRAY_BUFFER,particle_buffer);glBufferData(GL_ARRAY_BUFFER,triangles.size()*sizeof(SceneEffectVertex),triangles.data(),GL_STREAM_DRAW);count=GLsizei(triangles.size());
      }
      glUniformMatrix4fv(glGetUniformLocation(p,"view_projection"),1,GL_FALSE,projection->data());
      const auto stride=mesh ? sizeof(MeshVertex) : sizeof(SceneEffectVertex);
      glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(SceneEffectVertex,position)));
      glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(SceneEffectVertex,uv)));
      glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,stride,reinterpret_cast<void*>(offsetof(SceneEffectVertex,color)));
      glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
      if(mesh){glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(MeshVertex,normal)));glVertexAttribPointer(4,3,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<void*>(offsetof(MeshVertex,tangent)));glEnableVertexAttribArray(3);glEnableVertexAttribArray(4);}else {glDisableVertexAttribArray(3);glDisableVertexAttribArray(4);}
      glDrawArrays(GL_TRIANGLES,first,count);
    }
  }
  FrameQuad resolve_material(const MaterialShader& shader,const SceneEffectDraw* effect=nullptr) {
    FrameQuad q;q.shader=shader;
    for(unsigned slot=0;slot<4;++slot)if(q.shader.textures[slot]) {
      auto id=*q.shader.textures[slot];
      if(effect && effect->textures.contains(id)) {q.textures[slot]=FrameTexture{false,id};continue;}
      bool resolved=false;
      for(unsigned depth=0;depth<512;++depth) {
        require(id<project.operators.size() && graph.nodes[id].active,"invalid scene texture");const auto cmd=project.operators[id].command;
        if(cmd==1) {require(graph.nodes[id].links.size()==1 && graph.nodes[id].links[0],"unresolved scene texture Load");id=*graph.nodes[id].links[0];}
        else if(cmd==2 || cmd==3) {require(graph.nodes[id].inputs.size()==1,"invalid scene texture alias");id=graph.nodes[id].inputs[0];}
        else {resolved=true;break;}
      }
      require(resolved,"scene texture alias depth limit");
      q.textures[slot]=project.operators[id].command==0x26 ? FrameTexture{true,13+id} : FrameTexture{false,id};
    }
    return q;
  }
  void effect(const SceneEffectDraw& draw,const SceneMatrix& projection,const FrameTarget& target,const SceneCamera& camera) {
    if(draw.vertices.empty())return;
    const auto matrix=draw.normalized ? print_normalized_projection(target.width,target.height) : projection;
    std::optional<SceneFog> fog;if(draw.shader.words[0]&0x40){require(!draw.normalized,"normalized text fog is not ported");fog=scene_fog(camera,scene_identity());}
    quad(resolve_material(draw.shader,&draw),target.surface,&draw,&matrix,nullptr,nullptr,fog ? &*fog : nullptr);
  }
  void effects(const SceneFrame& scene,const FrameTarget& target) {
    const auto projection=scene_raster_projection(scene.camera,target.width,target.height);
    for(const auto& draw:scene.effects)effect(draw,projection,target,scene.camera);
  }
  const MeshGeometry& mesh_geometry(const std::shared_ptr<const Mesh>& mesh,const std::vector<SceneMeshBatch>& batches) {
    if(const auto found=geometry.find(mesh.get());found!=geometry.end())return found->second;
    MeshGeometry data;data.owner=mesh;std::vector<MeshVertex> triangles;const auto normals=mesh_normals(*mesh);
    std::size_t count=0;for(const auto& batch:batches)for(auto f:batch.faces)count+=(mesh->faces[f].count-2)*3;
    const auto bytes=count*sizeof(MeshVertex);require(bytes<=512u*1024*1024-geometry_bytes,"scene GPU geometry budget exceeded");triangles.reserve(count);
    std::vector<std::uint32_t> colors;colors.reserve(mesh->vertices.size());
    for(const auto& v:mesh->vertices)colors.push_back(mesh_vertex_color(v.color));
    for(const auto& batch:batches) {
      const auto first=GLint(triangles.size());
      for(auto f:batch.faces) {
        const auto& face=mesh->faces[f];
        for(unsigned c=1;c+1<face.count;++c)for(unsigned corner:{0u,c,c+1}) {
          const auto& v=mesh->vertices[face.vertices[corner]];
          triangles.push_back({{v.position,{v.uv[0],v.uv[1]},colors[face.vertices[corner]]},normals[f][corner],v.tangent});
        }
      }
      data.batches.emplace_back(first,GLsizei(triangles.size()-first));
    }
    glGenBuffers(1,&data.buffer);glBindBuffer(GL_ARRAY_BUFFER,data.buffer);glBufferData(GL_ARRAY_BUFFER,bytes,triangles.data(),GL_STATIC_DRAW);
    geometry_bytes+=bytes;++stats.mesh_uploads;return geometry.emplace(mesh.get(),std::move(data)).first->second;
  }
  const ShadowGeometry& shadow_geometry(const std::shared_ptr<const Mesh>& mesh,const SceneFrame& scene) {
    auto mask=shadow_face_mask(*mesh,scene.materials);auto found=shadows.find(mesh.get());
    if(found!=shadows.end() && found->second.mask==mask)return found->second;
    ShadowGeometry data;data.owner=mesh;data.mask=std::move(mask);data.mesh=prepare_shadow_mesh(*mesh,data.mask);
    const auto previous=found==shadows.end() ? 0 : found->second.bytes;
    data.bytes=data.mask.size()+data.mesh.vertices.size()*(sizeof(Vec3)+2*sizeof(std::array<float,4>))+data.mesh.planes.size()*sizeof(ShadowPlane)+data.mesh.edges.size()*sizeof(ShadowEdge)+(data.mesh.triangles.size()+data.mesh.triangle_planes.size())*sizeof(std::uint32_t);
    require(data.bytes<=512u*1024*1024-(geometry_bytes-previous),"scene shadow geometry budget exceeded");
    std::vector<std::array<float,4>> vertices;vertices.reserve(data.mesh.vertices.size()*2);
    for(const auto& v:data.mesh.vertices)for(float w:{1.f,0.f})vertices.push_back({v[0],v[1],v[2],w});
    glGenBuffers(1,&data.buffer);glBindBuffer(GL_ARRAY_BUFFER,data.buffer);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(vertices[0]),vertices.data(),GL_STATIC_DRAW);
    geometry_bytes=geometry_bytes-previous+data.bytes;
    ++stats.shadow_uploads;
    if(found!=shadows.end()){glDeleteBuffers(1,&found->second.buffer);found->second=std::move(data);return found->second;}
    return shadows.emplace(mesh.get(),std::move(data)).first->second;
  }
  void shadow(const SceneInstance& instance,const SceneFrame& scene,const MaterialShader& shader,const SceneMatrix& projection,const SceneLight& light,bool zfail) {
    require(shader.words[0]==0x70220 && !(shader.words[2]&7),"unsupported shadow material state");
    const auto& data=shadow_geometry(instance.mesh,scene);const auto position=light_model_vector(instance.world,light.position,true);
    const auto indices=shadow_indices(data.mesh,position);const auto count=zfail ? indices.indices.size() : indices.silhouette_count;if(!count)return;
    if(!shadow_program) {
      const auto v=gles::shader(GL_VERTEX_SHADER,shadow_vertex),f=gles::shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;out vec4 color;void main(){color=vec4(0.0);}");
      const auto p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
      GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p);throw std::runtime_error("shadow shader link failed");}shadow_program=p;++stats.program_compilations;
    }
    glUseProgram(shadow_program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,data.buffer);glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);glEnableVertexAttribArray(0);
    for(unsigned i=1;i<5;++i)glDisableVertexAttribArray(i);
    const auto matrix=scene_combine(projection,instance.world);glUniformMatrix4fv(glGetUniformLocation(shadow_program,"view_projection"),1,GL_FALSE,matrix.data());glUniform3fv(glGetUniformLocation(shadow_program,"light_position"),1,position.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,shadow_index_buffer);glBufferData(GL_ELEMENT_ARRAY_BUFFER,count*sizeof(std::uint32_t),indices.indices.data(),GL_STREAM_DRAW);
    // sMaterial11::Compile uses D3DCMP_LESS when SHADOWMASK is set. Equality
    // must fail so coplanar finite caps participate in the z-fail count.
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDepthMask(GL_FALSE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glFrontFace(GL_CW);
    glEnable(GL_STENCIL_TEST);glStencilMask(255);glStencilFunc(GL_ALWAYS,0,255);
    glStencilOpSeparate(GL_FRONT,GL_KEEP,zfail ? GL_DECR_WRAP : GL_KEEP,zfail ? GL_KEEP : GL_INCR_WRAP);
    glStencilOpSeparate(GL_BACK,GL_KEEP,zfail ? GL_INCR_WRAP : GL_KEEP,zfail ? GL_KEEP : GL_DECR_WRAP);
    glDrawElements(GL_TRIANGLES,GLsizei(count),GL_UNSIGNED_INT,nullptr);
  }
  void scene_materials(const SceneFrame& scene,const FrameTarget& target) {
    const auto draws=scene_draw_jobs(scene,target.width,target.height);const auto projection=scene_raster_projection(scene.camera,target.width,target.height);
    const auto restore_scissor=[&]{glScissor(0,surfaces.at(target.surface).height-target.height,target.width,target.height);};
    const auto insert=[&](unsigned usage) {
      restore_scissor();FrameQuad q;auto& w=q.shader.words;w[0]=0x30002|(usage==0 ? 0x3000 : 0x8000);w[9]=0x10;w[28]=0x00ffffff;w[23]=8;quad(q,target.surface);
    };
    const auto scissor=[&](std::optional<std::size_t> light) {
      restore_scissor();if(light){const auto& r=draws.lights[*light].rectangle;const int left=int(target.width*(1+r[0])*.5f),right=int(target.width*(1+r[2])*.5f),top=int(target.height*(1-r[3])*.5f),bottom=int(target.height*(1-r[1])*.5f);glScissor(left,surfaces.at(target.surface).height-bottom,right-left,bottom-top);}
    };
    const auto paint=[&](const auto& job) {
      if(job.effect) {effect(scene.effects[*job.effect],projection,target,scene.camera);return;}
      const auto& instance=scene.instances[job.instance];const auto& batches=draws.meshes.at(instance.mesh.get());
      const auto material=batches[job.batch].material;const auto& pass=(material ? scene.materials.at(*material) : scene_default_material()).passes[job.material_pass];
      try {
        if(pass.program==MaterialProgram::shadow) {
          require(job.light.has_value(),"shadow job needs a light");shadow(instance,scene,pass.shader,projection,draws.lights[*job.light].light,job.shadow_zfail);return;
        }
        const auto& data=mesh_geometry(instance.mesh,batches);const auto [first,count]=data.batches[job.batch];
        const auto matrix=scene_combine(projection,instance.world);const MeshDraw mesh{data.buffer,first,count};
        std::optional<LightDraw> setup;
        if(job.light) {
          const auto& l=draws.lights[*job.light].light;LightDraw draw;draw.directional=l.flags&4;
          draw.position=light_model_vector(instance.world,draw.directional ? l.direction : l.position,!draw.directional);draw.eye=light_model_vector(instance.world,{scene.camera.world[12],scene.camera.world[13],scene.camera.world[14]},true);
          const auto scale=.5f/l.range;for(unsigned c=0;c<3;++c)draw.attenuation[c]=draw.position[c]*(-scale)+.5f;draw.attenuation[3]=scale;
          draw.color=light_color(pass.shader,l);draw.specular=light_specular_parameters(as_float(pass.shader.words[32]));setup=draw;
        }
        std::optional<SceneFog> fog;if(pass.shader.words[0]&0x40)fog=scene_fog(scene.camera,instance.world);
        quad(resolve_material(pass.shader),target.surface,nullptr,&matrix,&mesh,setup ? &*setup : nullptr,fog ? &*fog : nullptr);
      }catch(const std::exception& e) {throw std::runtime_error("mesh material "+(material ? std::to_string(*material) : "default")+": "+e.what());}
    };
    if(project.runtime_graph) {
      // Final Theta predates BuildPaintJobs. Its Engine::Paint at 0x810475
      // finishes whole phases, including destination-alpha specular, before
      // drawing ordinary effects. The archived sort interleaves light-zero
      // effects with lighting and makes their alpha turn into white rectangles.
      const auto phase=[&](unsigned usage,std::optional<std::size_t> light={}) {
        scissor(light);
        for(const auto& job:draws.jobs)if(((job.sort_key>>16)&15)==usage && job.light==light)paint(job);
      };
      const auto mode=(scene.viewport_flags>>4)&3;
      if(mode==1)phase(7);
      else {
        phase(0);if(mode!=2)insert(0);
        for(std::size_t light=0;light<draws.lights.size();++light) {
          phase(3,light);glDisable(GL_STENCIL_TEST);phase(4,light);
          restore_scissor();glStencilMask(255);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);
        }
        phase(5);if(mode!=2)insert(5);
        phase(6);phase(7);
      }
    } else {
      const auto after=[&](unsigned pass,unsigned usage) {
        restore_scissor();if(!draws.inserts[pass])return;
        if(usage==0 || usage==5)insert(usage);
        if(usage==4){glStencilMask(255);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);}
        if(usage==3)glDisable(GL_STENCIL_TEST);
      };
      std::optional<std::tuple<unsigned,unsigned,std::optional<std::size_t>>> current;
      for(const auto& job:draws.jobs) {
        const auto group=std::tuple(job.sort_key>>24,(job.sort_key>>16)&15,job.light);
        if(current!=group) {
          if(current)after(std::get<0>(*current),std::get<1>(*current));
          current=group;scissor(job.light);
        }
        paint(job);
      }
      if(current)after(std::get<0>(*current),std::get<1>(*current));
    }
    restore_scissor();
  }

};
FrameRenderer::FrameRenderer(const Project& p,const Graph& g,const TextureOptions& options,Context mode):impl(std::make_unique<Impl>(p,g,options,mode)) {}
FrameRenderer::~FrameRenderer()=default;
FrameRenderer::Stats FrameRenderer::stats() const {return impl->stats;}
void FrameRenderer::reset_targets() {
  auto& s=*impl;s.activate();glDisable(GL_SCISSOR_TEST);
  glClearStencil(0);glStencilMask(255);
  for(const auto& [id,surface]:s.surfaces) {
    static_cast<void>(id);glBindFramebuffer(GL_FRAMEBUFFER,surface.fbo);
    clear({3,0xff000000});glClear(GL_STENCIL_BUFFER_BIT);
  }
  if(s.shared_depth.fbo) {glBindFramebuffer(GL_FRAMEBUFFER,s.shared_depth.fbo);clear({2,0},true);}
  glFinish();require(glGetError()==GL_NO_ERROR,"frame target reset failed");
}
std::vector<std::uint8_t> FrameRenderer::render(const DemoFrame& frame,bool diagnostic) {
  draw(frame,diagnostic);return readback();
}
void FrameRenderer::draw(const DemoFrame& frame,bool diagnostic) {
  auto& s=*impl;s.activate();glDisable(GL_DITHER);
  require(frame.surfaces.contains(0) && frame.surfaces.at(0).width==frame.width && frame.surfaces.at(0).height==frame.height,"invalid screen surface");
  for(const auto& [id,size]:frame.surfaces)s.surface(id,size);
  const bool released=s.project.runtime_graph;
  if(released)s.begin_release_frame(frame);
  std::optional<std::size_t> depth_owner;
  for(const auto& c:frame.commands) {
    const auto& dst=s.surfaces.at(c.target.surface);
    require(c.target.width && c.target.height && c.target.width<=dst.width && c.target.height<=dst.height,"frame viewport outside target");
    if(released && depth_owner!=c.target.surface) {
      if(depth_owner)s.copy_depth(s.surfaces.at(*depth_owner),s.shared_depth);
      s.copy_depth(s.shared_depth,dst);depth_owner=c.target.surface;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,dst.fbo);glViewport(0,dst.height-c.target.height,c.target.width,c.target.height);
    glEnable(GL_SCISSOR_TEST);glScissor(0,dst.height-c.target.height,c.target.width,c.target.height);
    const auto clear_target=[&](FrameClear value) {
      // Released BeginViewport clears a texture's full extent, then restores
      // its cropped draw rectangle. Screen clears use the draw rectangle.
      if(released && c.target.surface)glScissor(0,0,dst.width,dst.height);
      clear(value,released);
      glScissor(0,dst.height-c.target.height,c.target.width,c.target.height);
    };
    try {
      if(const auto* q=std::get_if<FrameQuad>(&c.draw))s.quad(*q,c.target.surface);
      else if(const auto* cl=std::get_if<FrameClear>(&c.draw))clear_target(*cl);
      else {const auto& scene=std::get<SceneFrame>(c.draw);clear_target({scene.viewport_flags&3,scene.background});if(diagnostic) {if(!scene.instances.empty())draw_scene_geometry(scene,c.target.width,c.target.height);s.effects(scene,c.target);}else s.scene_materials(scene,c.target);}
      require(glGetError()==GL_NO_ERROR,"frame draw produced an OpenGL error");
    }catch(const std::exception& e) {throw std::runtime_error("operator "+std::to_string(c.operator_id)+": "+e.what());}
  }
  if(depth_owner) {s.copy_depth(s.surfaces.at(*depth_owner),s.shared_depth);require(glGetError()==GL_NO_ERROR,"shared depth transfer failed");}
}
std::vector<std::uint8_t> FrameRenderer::readback() {
  auto& s=*impl;s.activate();const auto& screen=s.surfaces.at(0);
  glBindFramebuffer(GL_FRAMEBUFFER,screen.fbo);std::vector<std::uint8_t> pixels(std::size_t(screen.width)*screen.height*4);
  glReadPixels(0,0,screen.width,screen.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());require(glGetError()==GL_NO_ERROR,"frame readback failed");
  for(unsigned y=0;y<screen.height/2;++y)std::swap_ranges(pixels.begin()+std::size_t(y)*screen.width*4,pixels.begin()+std::size_t(y+1)*screen.width*4,pixels.begin()+std::size_t(screen.height-y-1)*screen.width*4);
  return pixels;
}
void FrameRenderer::present(unsigned width,unsigned height) {
  require(width && height && width<=16384 && height<=16384,"invalid drawable size");
  auto& s=*impl;s.activate();const auto& screen=s.surfaces.at(0);
  unsigned w=width,h=height;
  if(std::uint64_t(width)*screen.height>std::uint64_t(height)*screen.width)w=std::uint64_t(height)*screen.width/screen.height;
  else h=std::uint64_t(width)*screen.height/screen.width;
  const auto x=(width-w)/2,y=(height-h)/2;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,screen.fbo);
  glBlitFramebuffer(0,0,screen.width,screen.height,x,y,x+w,y+h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
  require(glGetError()==GL_NO_ERROR,"frame presentation failed");
  glBindFramebuffer(GL_FRAMEBUFFER,0);
}
} // namespace wz3
