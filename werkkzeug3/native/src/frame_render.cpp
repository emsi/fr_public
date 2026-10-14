// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/frame.hpp"
#include "wz3/texture.hpp"
#include "wz3/print.hpp"
#include "wz3/scene_draw.hpp"
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
  uniform mat3 uv_transform[4];
  uniform vec4 flip_texture;
  out vec2 material_uv[4];
  out vec4 material_vertex_color;
  void main() {
    gl_Position=view_projection*vec4(position,1.0);
    for(int i=0;i<4;++i) {
      material_uv[i]=(uv_transform[i]*vec3(uv,1.0)).xy;
      if(flip_texture[i]>0.5)material_uv[i].y=1.0-material_uv[i].y;
    }
    material_vertex_color=color.bgra;
  })";
void clear(FrameClear c) {
  glDepthMask(GL_TRUE);glClearDepthf(1);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glClearColor(((c.color>>16)&255)/255.0f,((c.color>>8)&255)/255.0f,(c.color&255)/255.0f,(c.color>>24)/255.0f);
  glClear(((c.flags&1) ? GL_COLOR_BUFFER_BIT : 0)|((c.flags&2) ? GL_DEPTH_BUFFER_BIT : 0));
}
}
struct FrameRenderer::Impl {
  const Project& project;const Graph& graph;TextureOptions texture_options;gles::Context context{1,1};
  struct Surface {GLuint color{},depth{},fbo{};unsigned width{},height{};};
  std::map<std::size_t,Surface> surfaces;
  std::map<std::size_t,GLuint> bitmaps;
  std::map<std::size_t,std::shared_ptr<const Texture>> supplied_bitmaps;
  std::map<std::size_t,std::size_t> bitmap_sizes;
  std::map<std::string,GLuint> programs;
  struct MeshGeometry {
    std::shared_ptr<const Mesh> owner;GLuint buffer{};
    std::vector<std::pair<GLint,GLsizei>> batches;
  };
  struct MeshDraw {GLuint buffer;GLint first;GLsizei count;};
  std::map<const Mesh*,MeshGeometry> geometry;
  std::size_t geometry_bytes{};
  GLuint vao{},particle_buffer{};std::array<GLuint,4> samplers{};
  std::size_t surface_bytes{},bitmap_bytes{};
  Impl(const Project& p,const Graph& g,const TextureOptions& options):project(p),graph(g),texture_options(options) {
    std::cerr<<"GL vendor: "<<glGetString(GL_VENDOR)<<"\nGL renderer: "<<glGetString(GL_RENDERER)<<"\nGL version: "<<glGetString(GL_VERSION)<<'\n';
    glGenBuffers(1,&particle_buffer);glGenVertexArrays(1,&vao);glGenSamplers(4,samplers.data());
  }
  ~Impl() {
    eglMakeCurrent(context.display,context.surface,context.surface,context.context);
    for(const auto& [id,s]:surfaces) {static_cast<void>(id);destroy(s);}
    for(const auto& [id,t]:bitmaps) {static_cast<void>(id);glDeleteTextures(1,&t);}
    for(const auto& [source,p]:programs) {static_cast<void>(source);glDeleteProgram(p);}
    for(const auto& [id,g]:geometry) {static_cast<void>(id);glDeleteBuffers(1,&g.buffer);}
    glDeleteBuffers(1,&particle_buffer);glDeleteSamplers(4,samplers.data());glDeleteVertexArrays(1,&vao);
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
  GLuint bitmap(std::size_t id,std::shared_ptr<const Texture> supplied={}) {
    const auto found=bitmaps.find(id);
    if(found!=bitmaps.end() && (!supplied || (supplied_bitmaps.contains(id) && supplied_bitmaps.at(id)==supplied)))return found->second;
    const auto t=supplied ? supplied : evaluate_texture(project,graph,id,texture_options);const auto pixels=rgba8(*t);
    const auto previous=bitmap_sizes.contains(id) ? bitmap_sizes.at(id) : 0;
    require(pixels.size()<=256u*1024*1024-(bitmap_bytes-previous),"frame bitmap budget exceeded");
    GLuint texture=0;if(found!=bitmaps.end())texture=found->second;else glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,t->width,t->height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glGenerateMipmap(GL_TEXTURE_2D);
    bitmap_bytes=bitmap_bytes-previous+pixels.size();bitmap_sizes[id]=pixels.size();bitmaps[id]=texture;if(supplied)supplied_bitmaps[id]=std::move(supplied);return texture;
  }
  GLuint program(const MaterialShader& s,bool particle) {
    const auto source=material_fragment_shader(s);const auto key=(particle ? "particle:" : "quad:")+source;if(auto found=programs.find(key);found!=programs.end())return found->second;
    require(programs.size()<4096,"frame shader cache limit");
    const auto vertex=gles::shader(GL_VERTEX_SHADER,particle ? particle_vertex : quad_vertex);GLuint fragment;
    try {fragment=gles::shader(GL_FRAGMENT_SHADER,source.c_str());}catch(...) {glDeleteShader(vertex);throw;}
    const auto p=glCreateProgram();glAttachShader(p,vertex);glAttachShader(p,fragment);glLinkProgram(p);glDeleteShader(vertex);glDeleteShader(fragment);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("frame shader link failed: ")+log);}
    programs.emplace(key,p);return p;
  }
  void quad(const FrameQuad& q,std::size_t destination,const SceneEffectDraw* effect=nullptr,const SceneMatrix* projection=nullptr,const MeshDraw* mesh=nullptr) {
    const auto& s=q.shader;const auto flags=s.words[0];
    require(!(flags&0xfc0058),"unlit draw fog, shadow or depth bias is not ported");
    const bool vertices=effect || mesh;const auto p=program(s,vertices);glUseProgram(p);glBindVertexArray(vao);
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
      glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(SceneEffectVertex),reinterpret_cast<void*>(offsetof(SceneEffectVertex,position)));
      glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(SceneEffectVertex),reinterpret_cast<void*>(offsetof(SceneEffectVertex,uv)));
      glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(SceneEffectVertex),reinterpret_cast<void*>(offsetof(SceneEffectVertex,color)));
      glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
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
  void effect(const SceneEffectDraw& draw,const SceneMatrix& projection,const FrameTarget& target) {
    if(draw.vertices.empty())return;
    const auto matrix=draw.normalized ? print_normalized_projection(target.width,target.height) : projection;
    quad(resolve_material(draw.shader,&draw),target.surface,&draw,&matrix);
  }
  void effects(const SceneFrame& scene,const FrameTarget& target) {
    const auto projection=scene_view_projection(scene.camera,target.width,target.height);
    for(const auto& draw:scene.effects)effect(draw,projection,target);
  }
  const MeshGeometry& mesh_geometry(const std::shared_ptr<const Mesh>& mesh,const std::vector<SceneMeshBatch>& batches) {
    if(const auto found=geometry.find(mesh.get());found!=geometry.end())return found->second;
    MeshGeometry data;data.owner=mesh;std::vector<SceneEffectVertex> triangles;
    std::size_t count=0;for(const auto& batch:batches)for(auto f:batch.faces)count+=(mesh->faces[f].count-2)*3;
    const auto bytes=count*sizeof(SceneEffectVertex);require(bytes<=512u*1024*1024-geometry_bytes,"scene GPU geometry budget exceeded");triangles.reserve(count);
    std::vector<std::uint32_t> colors;colors.reserve(mesh->vertices.size());
    for(const auto& v:mesh->vertices)colors.push_back(mesh_vertex_color(v.color));
    for(const auto& batch:batches) {
      const auto first=GLint(triangles.size());
      for(auto f:batch.faces) {
        const auto& face=mesh->faces[f];
        for(unsigned c=1;c+1<face.count;++c)for(unsigned corner:{0u,c,c+1}) {
          const auto& v=mesh->vertices[face.vertices[corner]];
          triangles.push_back({v.position,{v.uv[0],v.uv[1]},colors[face.vertices[corner]]});
        }
      }
      data.batches.emplace_back(first,GLsizei(triangles.size()-first));
    }
    glGenBuffers(1,&data.buffer);glBindBuffer(GL_ARRAY_BUFFER,data.buffer);glBufferData(GL_ARRAY_BUFFER,bytes,triangles.data(),GL_STATIC_DRAW);
    geometry_bytes+=bytes;return geometry.emplace(mesh.get(),std::move(data)).first->second;
  }
  void scene_materials(const SceneFrame& scene,const FrameTarget& target) {
    const auto draws=scene_unlit_jobs(scene);const auto projection=scene_view_projection(scene.camera,target.width,target.height);
    for(const auto& job:draws.jobs) {
      if(job.effect) {effect(scene.effects[*job.effect],projection,target);continue;}
      const auto& instance=scene.instances[job.instance];const auto& batches=draws.meshes.at(instance.mesh.get());
      const auto material=*batches[job.batch].material;const auto& pass=scene.materials.at(material).passes[job.material_pass];
      try {
        const auto& data=mesh_geometry(instance.mesh,batches);const auto [first,count]=data.batches[job.batch];
        const auto matrix=scene_combine(projection,instance.world);const MeshDraw mesh{data.buffer,first,count};
        quad(resolve_material(pass.shader),target.surface,nullptr,&matrix,&mesh);
      }catch(const std::exception& e) {throw std::runtime_error("mesh material "+std::to_string(material)+": "+e.what());}
    }
  }

};
FrameRenderer::FrameRenderer(const Project& p,const Graph& g,const TextureOptions& options):impl(std::make_unique<Impl>(p,g,options)) {}
FrameRenderer::~FrameRenderer()=default;
std::vector<std::uint8_t> FrameRenderer::render(const DemoFrame& frame,bool diagnostic) {
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
      else {const auto& scene=std::get<SceneFrame>(c.draw);clear({scene.viewport_flags&3,scene.background});if(diagnostic) {if(!scene.instances.empty())draw_scene_geometry(scene,c.target.width,c.target.height);s.effects(scene,c.target);}else s.scene_materials(scene,c.target);}
      require(glGetError()==GL_NO_ERROR,"frame draw produced an OpenGL error");
    }catch(const std::exception& e) {throw std::runtime_error("operator "+std::to_string(c.operator_id)+": "+e.what());}
  }
  glBindFramebuffer(GL_FRAMEBUFFER,s.surfaces.at(0).fbo);std::vector<std::uint8_t> pixels(std::size_t(frame.width)*frame.height*4);
  glReadPixels(0,0,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());require(glGetError()==GL_NO_ERROR,"frame readback failed");
  for(unsigned y=0;y<frame.height/2;++y)std::swap_ranges(pixels.begin()+std::size_t(y)*frame.width*4,pixels.begin()+std::size_t(y+1)*frame.width*4,pixels.begin()+std::size_t(frame.height-y-1)*frame.width*4);
  return pixels;
}
} // namespace wz3
