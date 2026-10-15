// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Headless native Linux graphics path; no X server or Windows runtime required.
#include "wz3/texture.hpp"
#include "wz3/mesh.hpp"
#include "wz3/scene.hpp"
#include "gles.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace wz3 {
namespace {
using gles::Context;
using gles::shader;
}

std::vector<std::uint8_t> render_texture(const Texture& texture) {
  Context context(texture.width, texture.height);
  std::cerr << "GL vendor: " << glGetString(GL_VENDOR) << "\nGL renderer: " << glGetString(GL_RENDERER)
            << "\nGL version: " << glGetString(GL_VERSION) << '\n';
  GLint max_size = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
  if (texture.width > unsigned(max_size) || texture.height > unsigned(max_size))
    throw std::runtime_error("texture exceeds GPU size limit");
  const auto source = rgba8(texture);
  GLuint tex = 0, vao = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, source.data());
  const auto vertex = shader(GL_VERTEX_SHADER, R"(#version 300 es
    void main() {
      vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
      gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    })");
  const auto fragment = shader(GL_FRAGMENT_SHADER, R"(#version 300 es
    precision highp float;
    uniform highp sampler2D image;
    out vec4 color;
    void main() { color = texelFetch(image, ivec2(gl_FragCoord.xy), 0); }
    )");
  const auto program = glCreateProgram();
  glAttachShader(program, vertex); glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex); glDeleteShader(fragment);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[4096]{};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    throw std::runtime_error(std::string("shader link failed: ") + log);
  }
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glViewport(0, 0, texture.width, texture.height);
  glDisable(GL_DITHER);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "image"), 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("incomplete preview framebuffer");
  glDrawArrays(GL_TRIANGLES, 0, 3);
  std::vector<std::uint8_t> output(source.size());
  glReadPixels(0, 0, texture.width, texture.height, GL_RGBA, GL_UNSIGNED_BYTE, output.data());
  const auto error = glGetError();
  glDeleteVertexArrays(1, &vao);
  glDeleteTextures(1, &tex);
  glDeleteProgram(program);
  if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL render/readback error " + std::to_string(error));
  if (output != source) {
    const auto mismatch = std::mismatch(output.begin(), output.end(), source.begin());
    throw std::runtime_error("GPU readback differs at channel " + std::to_string(mismatch.first - output.begin()));
  }
  std::cerr << "GPU readback: all " << output.size() / 4 << " RGBA pixels match CPU texture\n";
  return output;
}

std::vector<std::uint8_t> render_mesh(const Mesh& mesh, unsigned width, unsigned height) {
  validate_mesh(mesh);
  if (mesh.faces.empty() || width < 16 || height < 16 || width > 4096 || height > 4096)
    throw std::runtime_error("invalid mesh preview size or empty geometry");
  const auto bounds = mesh_bounds(mesh);
  Vec3 center{}, extent{};
  for (unsigned c = 0; c < 3; ++c) {
    center[c] = bounds.min[c]*0.5f + bounds.max[c]*0.5f;
    extent[c] = bounds.max[c]*0.5f - bounds.min[c]*0.5f;
  }
  const double radius = std::hypot(double(extent[0]),double(extent[1]),double(extent[2]));
  if (!std::isfinite(radius) || radius < 1e-12) throw std::runtime_error("mesh preview needs finite, nonzero extent");
  Context context(width,height,true);
  std::cerr << "GL vendor: " << glGetString(GL_VENDOR) << "\nGL renderer: " << glGetString(GL_RENDERER)
            << "\nGL version: " << glGetString(GL_VERSION) << '\n';
  GLint depth_bits = 0;
  glGetIntegerv(GL_DEPTH_BITS,&depth_bits);
  if (depth_bits < 16) throw std::runtime_error("mesh preview needs a depth buffer");
  std::vector<std::uint32_t> indices;
  struct Vertex { Vec3 position,uv,normal; };
  std::vector<Vertex> vertices;
  const auto normals=mesh_normals(mesh);
  vertices.reserve(mesh.faces.size()*4);
  indices.reserve(mesh.faces.size()*6);
  for (std::size_t f=0;f<mesh.faces.size();++f) {
    const auto& face=mesh.faces[f];
    const auto first=std::uint32_t(vertices.size());
    for (unsigned c=0;c<face.count;++c) {
      const auto& v=mesh.vertices[face.vertices[c]];
      vertices.push_back({v.position,v.uv,normals[f][c]});
    }
    for (unsigned i = 1; i+1 < face.count; ++i) {
      indices.push_back(first); indices.push_back(first+i); indices.push_back(first+i+1);
    }
  }
  const auto vertex = shader(GL_VERTEX_SHADER,R"(#version 300 es
    layout(location=0) in vec3 position;
    layout(location=1) in vec3 uv;
    layout(location=2) in vec3 normal;
    uniform vec3 center;
    uniform vec3 scale;
    out vec3 world_normal;
    out vec2 texcoord;
    void main() {
      vec3 p = position - center;
      vec3 view = vec3(dot(p,vec3(0.8,0.0,-0.6)),
                       dot(p,vec3(-0.3,0.8660254,-0.4)),
                       -dot(p,vec3(0.5196152,0.5,0.6928203)));
      gl_Position = vec4(view * scale,1.0);
      world_normal = normal;
      texcoord = uv.xy;
    })");
  const auto fragment = shader(GL_FRAGMENT_SHADER,R"(#version 300 es
    precision highp float;
    in vec3 world_normal;
    in vec2 texcoord;
    out vec4 color;
    void main() {
      vec3 normal = normalize(world_normal);
      float light = 0.3 + 0.7*abs(dot(normal,normalize(vec3(-0.4,0.8,0.5))));
      float checker = mod(floor(texcoord.x*8.0)+floor(texcoord.y*8.0),2.0);
      vec3 base = mix(vec3(0.16,0.38,0.58),vec3(0.55,0.8,0.95),checker);
      color = vec4(base*light,1.0);
    })");
  const auto program = glCreateProgram();
  glAttachShader(program,vertex); glAttachShader(program,fragment); glLinkProgram(program);
  glDeleteShader(vertex); glDeleteShader(fragment);
  GLint linked = 0; glGetProgramiv(program,GL_LINK_STATUS,&linked);
  if (!linked) {
    char log[4096]{}; glGetProgramInfoLog(program,sizeof(log),nullptr,log);
    throw std::runtime_error(std::string("mesh shader link failed: ")+log);
  }
  GLuint vao = 0, vertex_buffer = 0, index_buffer = 0;
  glGenVertexArrays(1,&vao); glBindVertexArray(vao);
  glGenBuffers(1,&vertex_buffer); glBindBuffer(GL_ARRAY_BUFFER,vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,position)));
  glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,uv)));
  glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));
  glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
  glGenBuffers(1,&index_buffer); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(std::uint32_t),indices.data(),GL_STATIC_DRAW);
  glUseProgram(program);
  glUniform3fv(glGetUniformLocation(program,"center"),1,center.data());
  const float fit = 0.85 / radius, aspect = float(width)/height;
  glUniform3f(glGetUniformLocation(program,"scale"),fit/std::max(1.0f,aspect),fit*std::min(1.0f,aspect),fit*0.5f);
  glViewport(0,0,width,height);
  glDisable(GL_DITHER); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
  glClearColor(12.0f/255,15.0f/255,21.0f/255,1); glClearDepthf(1);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("incomplete mesh framebuffer");
  glDrawElements(GL_TRIANGLES,indices.size(),GL_UNSIGNED_INT,nullptr);
  std::vector<std::uint8_t> output(std::size_t(width)*height*4);
  glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,output.data());
  const auto error = glGetError();
  glDeleteBuffers(1,&vertex_buffer); glDeleteBuffers(1,&index_buffer);
  glDeleteVertexArrays(1,&vao); glDeleteProgram(program);
  if (error != GL_NO_ERROR) throw std::runtime_error("mesh GPU render/readback error " + std::to_string(error));
  std::size_t visible = 0;
  for (std::size_t i = 0; i < output.size(); i += 4)
    visible += output[i] != 12 || output[i+1] != 15 || output[i+2] != 21;
  if (!visible) throw std::runtime_error("mesh GPU preview produced only background pixels");
  // Readback rows start at the lower left; PPM consumers expect top to bottom.
  for (unsigned y = 0; y < height/2; ++y)
    std::swap_ranges(output.begin()+std::size_t(y)*width*4,output.begin()+std::size_t(y+1)*width*4,
                     output.begin()+std::size_t(height-y-1)*width*4);
  std::cerr << "Mesh GPU readback: " << indices.size()/3 << " triangles, " << visible
            << " visible pixels, " << depth_bits << "-bit depth buffer\n";
  return output;
}
void draw_scene_geometry(const SceneFrame& frame,unsigned width,unsigned height) {
  const auto view_projection=scene_raster_projection(frame.camera,width,height);
  const auto vertex=shader(GL_VERTEX_SHADER,R"(#version 300 es
    layout(location=0) in vec3 position;
    layout(location=1) in vec3 uv;
    layout(location=2) in vec3 normal;
    uniform mat4 model;
    uniform mat4 view_projection;
    out vec3 world_normal;
    out vec2 texcoord;
    void main() {
      gl_Position = view_projection * model * vec4(position,1.0);
      mat3 cofactors = mat3(cross(model[1].xyz,model[2].xyz),
                           cross(model[2].xyz,model[0].xyz),
                           cross(model[0].xyz,model[1].xyz));
      vec3 n = cofactors * normal;
      world_normal = dot(n,n)>1e-20 ? n : vec3(1.0,0.0,0.0);
      texcoord = uv.xy;
    })");
  const auto fragment=shader(GL_FRAGMENT_SHADER,R"(#version 300 es
    precision highp float;
    in vec3 world_normal;
    in vec2 texcoord;
    out vec4 color;
    void main() {
      float light=0.3+0.7*abs(dot(normalize(world_normal),normalize(vec3(-0.4,0.8,0.5))));
      float checker=mod(floor(texcoord.x*8.0)+floor(texcoord.y*8.0),2.0);
      color=vec4(mix(vec3(0.16,0.38,0.58),vec3(0.55,0.8,0.95),checker)*light,1.0);
    })");
  const auto program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
  glDeleteShader(vertex);glDeleteShader(fragment);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
  if(!linked) {char log[4096]{};glGetProgramInfoLog(program,sizeof(log),nullptr,log);throw std::runtime_error(std::string("scene shader link failed: ")+log);}
  glUseProgram(program);glUniformMatrix4fv(glGetUniformLocation(program,"view_projection"),1,GL_FALSE,view_projection.data());
  const auto model_location=glGetUniformLocation(program,"model");
  glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("incomplete scene framebuffer");
  struct Buffers {GLuint vao{},vertices{},indices{};GLsizei count{};};
  struct Vertex {Vec3 position,uv,normal;};
  std::map<const Mesh*,Buffers> geometry;std::size_t allocated=0;
  for(const auto& instance:frame.instances) {
    if(!instance.mesh)throw std::runtime_error("missing scene geometry");
    auto [found,created]=geometry.try_emplace(instance.mesh.get());auto& buffer=found->second;
    if(created) {
      const auto& mesh=*instance.mesh;validate_mesh(mesh);const auto normals=mesh_normals(mesh);
      std::vector<Vertex> vertices;std::vector<std::uint32_t> indices;
      for(std::size_t f=0;f<mesh.faces.size();++f) {
        const auto& face=mesh.faces[f];const auto first=std::uint32_t(vertices.size());
        for(unsigned c=0;c<face.count;++c) {const auto& v=mesh.vertices[face.vertices[c]];vertices.push_back({v.position,v.uv,normals[f][c]});}
        for(unsigned c=1;c+1<face.count;++c) {indices.push_back(first);indices.push_back(first+c);indices.push_back(first+c+1);}
      }
      allocated+=vertices.size()*sizeof(Vertex)+indices.size()*sizeof(std::uint32_t);
      if(allocated>512u*1024*1024)throw std::runtime_error("scene GPU geometry budget exceeded");
      buffer.count=GLsizei(indices.size());glGenVertexArrays(1,&buffer.vao);glBindVertexArray(buffer.vao);
      glGenBuffers(1,&buffer.vertices);glBindBuffer(GL_ARRAY_BUFFER,buffer.vertices);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
      glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,position)));
      glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,uv)));
      glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));
      glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
      glGenBuffers(1,&buffer.indices);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffer.indices);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(std::uint32_t),indices.data(),GL_STATIC_DRAW);
    }else glBindVertexArray(buffer.vao);
    glUniformMatrix4fv(model_location,1,GL_FALSE,instance.world.data());glDrawElements(GL_TRIANGLES,buffer.count,GL_UNSIGNED_INT,nullptr);
  }
  for(auto [mesh,b]:geometry) {static_cast<void>(mesh);glDeleteVertexArrays(1,&b.vao);glDeleteBuffers(1,&b.vertices);glDeleteBuffers(1,&b.indices);}
  glDeleteProgram(program);const auto error=glGetError();if(error!=GL_NO_ERROR)throw std::runtime_error("scene GPU error "+std::to_string(error));
}
std::vector<std::uint8_t> render_scene(const SceneFrame& frame,unsigned width,unsigned height) {
  if(!frame.effects.empty())throw std::runtime_error("effect scenes require the wz3-frame-render compositor");
  if(width<16 || height<16 || width>4096 || height>4096)throw std::runtime_error("invalid scene render size");
  Context context(width,height,true);
  std::cerr<<"GL renderer: "<<glGetString(GL_RENDERER)<<'\n';
  glViewport(0,0,width,height);glDepthMask(GL_TRUE);glClearDepthf(1);
  glClearColor(((frame.background>>16)&255)/255.0f,((frame.background>>8)&255)/255.0f,(frame.background&255)/255.0f,1);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  draw_scene_geometry(frame,width,height);
  std::vector<std::uint8_t> output(std::size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,output.data());
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("scene readback failed");
  for(unsigned y=0;y<height/2;++y)std::swap_ranges(output.begin()+std::size_t(y)*width*4,output.begin()+std::size_t(y+1)*width*4,output.begin()+std::size_t(height-y-1)*width*4);
  return output;
}
std::vector<std::uint8_t> render_material_quad(const Material& material,
    const std::map<std::size_t,std::shared_ptr<const Texture>>& textures,unsigned width,unsigned height,std::uint32_t background) {
  if(!width || !height || width>4096 || height>4096)throw std::runtime_error("invalid material render size");
  // Validate before creating a context or changing any render state.
  std::vector<std::string> fragments;std::vector<std::array<float,36>> transforms;
  for(const auto& pass:material.passes) {
    if(pass.usage!=MaterialUsage::other || (pass.shader.words[0]&0xfc0078))throw std::runtime_error("material quad needs an unlit pass without fog, shadow or depth bias");
    if(pass.shader.words[1]&0xf30)throw std::runtime_error("environment/projection coordinates require a mesh preview");
    fragments.push_back(material_fragment_shader(pass.shader));std::array<float,36> matrices{};
    for(unsigned i=0;i<4;++i) {const auto uv=material_uv_matrix(pass.shader,i);std::copy(uv.begin(),uv.end(),matrices.begin()+i*9);}
    transforms.push_back(matrices);
    for(auto id:pass.shader.textures)if(id && (!textures.contains(*id) || !textures.at(*id)))throw std::runtime_error("missing material texture");
  }
  Context context(width,height,true);
  std::cerr<<"GL vendor: "<<glGetString(GL_VENDOR)<<"\nGL renderer: "<<glGetString(GL_RENDERER)<<"\nGL version: "<<glGetString(GL_VERSION)<<'\n';
  glViewport(0,0,width,height);glDisable(GL_DITHER);glEnable(GL_DEPTH_TEST);glClearDepthf(1);
  glClearColor(((background>>16)&255)/255.0f,((background>>8)&255)/255.0f,(background&255)/255.0f,(background>>24)/255.0f);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glFrontFace(GL_CCW);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("incomplete material framebuffer");
  GLuint vao=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  std::map<std::size_t,GLuint> uploaded;std::size_t bytes=0;
  const char* vertex_source=R"(#version 300 es
    uniform mat3 uv_transform[4];
    out vec2 material_uv[4];
    out vec4 material_vertex_color;
    void main() {
      vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
      gl_Position=vec4(p*2.0-1.0,0.0,1.0);
      for(int i=0;i<4;++i)material_uv[i]=(uv_transform[i]*vec3(p.x,1.0-p.y,1.0)).xy;
      material_vertex_color=vec4(1.0);
    })";
  for(std::size_t index=0;index<material.passes.size();++index) {
    const auto& s=material.passes[index].shader;const auto flags=s.words[0];
    const auto vertex=shader(GL_VERTEX_SHADER,vertex_source),fragment=shader(GL_FRAGMENT_SHADER,fragments[index].c_str());
    const auto program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
    glDeleteShader(vertex);glDeleteShader(fragment);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked) {char log[4096]{};glGetProgramInfoLog(program,sizeof(log),nullptr,log);throw std::runtime_error(std::string("material shader link failed: ")+log);}
    glUseProgram(program);glUniformMatrix3fv(glGetUniformLocation(program,"uv_transform[0]"),4,GL_FALSE,transforms[index].data());
    std::array<float,16> colors{};
    for(unsigned i=0;i<4;++i)for(unsigned c=0;c<4;++c)colors[i*4+c]=((s.words[28+i]>>(c==0 ? 16 : c==1 ? 8 : c==2 ? 0 : 24))&255)/255.0f;
    glUniform4fv(glGetUniformLocation(program,"material_colors[0]"),4,colors.data());
    std::array<GLuint,4> samplers{};glGenSamplers(4,samplers.data());
    for(unsigned i=0;i<4;++i)if(s.textures[i]) {
      const auto id=*s.textures[i];glActiveTexture(GL_TEXTURE0+i);auto [found,created]=uploaded.try_emplace(id);
      if(created) {
        const auto& source=*textures.at(id);const auto levels=color_texture_levels(source);
        for(const auto& level:levels)bytes+=level.pixels.size();
        if(bytes>256u*1024*1024)throw std::runtime_error("material texture upload budget exceeded");
        glGenTextures(1,&found->second);glBindTexture(GL_TEXTURE_2D,found->second);
        for(std::size_t level=0;level<levels.size();++level){const auto& mip=levels[level];glTexImage2D(GL_TEXTURE_2D,GLint(level),GL_RGBA8,mip.width,mip.height,0,GL_RGBA,GL_UNSIGNED_BYTE,mip.pixels.data());}
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,GLint(levels.size()-1));
      } else glBindTexture(GL_TEXTURE_2D,found->second);
      const auto tf=s.words[4+i];const auto filter=tf&7;const auto sampler=samplers[i];
      constexpr GLint min_filter[]={GL_NEAREST,GL_LINEAR,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR,GL_NEAREST_MIPMAP_NEAREST,GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST,GL_NEAREST};
      glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,min_filter[filter]);
      glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,(filter==1 || filter==2 || filter==3 || filter==5) ? GL_LINEAR : GL_NEAREST);
      glSamplerParameteri(sampler,GL_TEXTURE_WRAP_S,(tf&0x100) ? GL_CLAMP_TO_EDGE : GL_REPEAT);glSamplerParameteri(sampler,GL_TEXTURE_WRAP_T,(tf&0x100) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
      if(filter==3) {
        const auto extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if(!extensions || std::string_view(extensions).find("GL_EXT_texture_filter_anisotropic")==std::string_view::npos)throw std::runtime_error("material needs anisotropic texture filtering");
        GLfloat maximum=1;glGetFloatv(0x84ff,&maximum);glSamplerParameterf(sampler,0x84fe,std::min(4.0f,maximum));
      }
      glBindSampler(i,sampler);glUniform1i(glGetUniformLocation(program,("material_tex"+std::to_string(i)).c_str()),GLint(i));
    }
    if(flags&2)glDisable(GL_CULL_FACE);else {glEnable(GL_CULL_FACE);glCullFace((flags&4) ? GL_FRONT : GL_BACK);}
    glDepthMask((flags&0x100) ? GL_TRUE : GL_FALSE);glDepthFunc(!(flags&0x200) ? GL_ALWAYS : (flags&0x80) ? GL_EQUAL : GL_LEQUAL);
    const auto blend=(flags>>12)&15;
    if(!blend)glDisable(GL_BLEND);
    else {
      constexpr GLenum src[]={0,GL_SRC_ALPHA,GL_ONE,GL_ZERO,GL_DST_COLOR,GL_ONE,GL_ONE,GL_ONE_MINUS_DST_COLOR,GL_DST_ALPHA,GL_SRC_ALPHA};
      constexpr GLenum dst[]={0,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_SRC_COLOR,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_ONE,GL_ONE_MINUS_SRC_COLOR,GL_ONE,GL_ONE};
      if(blend>9)throw std::runtime_error("unsupported material blend mode");
      glEnable(GL_BLEND);glBlendEquation(blend==6 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);glBlendFunc(src[blend],dst[blend]);
    }
    glDrawArrays(GL_TRIANGLES,0,3);glDeleteSamplers(4,samplers.data());glDeleteProgram(program);
  }
  std::vector<std::uint8_t> output(std::size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,output.data());
  const auto error=glGetError();for(const auto& [id,texture]:uploaded) {static_cast<void>(id);glDeleteTextures(1,&texture);}glDeleteVertexArrays(1,&vao);
  if(error!=GL_NO_ERROR)throw std::runtime_error("material GPU error "+std::to_string(error));
  for(unsigned y=0;y<height/2;++y)std::swap_ranges(output.begin()+std::size_t(y)*width*4,output.begin()+std::size_t(y+1)*width*4,output.begin()+std::size_t(height-y-1)*width*4);
  std::cerr<<"Material GPU readback: "<<material.passes.size()<<" unlit passes, "<<uploaded.size()<<" textures\n";return output;
}
} // namespace wz3
