// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Headless native Linux graphics path; no X server or Windows runtime required.
#include "wz3/texture.hpp"
#include "wz3/mesh.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace wz3 {
namespace {
void egl_check(bool ok, const char* action) {
  if (!ok) {
    std::ostringstream message;
    message << action << " failed (EGL 0x" << std::hex << eglGetError() << ')';
    throw std::runtime_error(message.str());
  }
}
struct Context {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
  Context(unsigned width, unsigned height, bool depth = false) {
    try {
      const char* extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
      if (!extensions || std::string_view(extensions).find("EGL_MESA_platform_surfaceless") == std::string_view::npos)
        throw std::runtime_error("EGL_MESA_platform_surfaceless is required by this headless preview");
      display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
      egl_check(display != EGL_NO_DISPLAY, "eglGetPlatformDisplay");
      EGLint major, minor;
      egl_check(eglInitialize(display, &major, &minor), "eglInitialize");
      egl_check(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
      const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, depth ? 24 : 0,
        EGL_SAMPLE_BUFFERS, 0, EGL_SAMPLES, 0, EGL_NONE
      };
      EGLConfig config;
      EGLint count;
      egl_check(eglChooseConfig(display, attributes, &config, 1, &count), "eglChooseConfig");
      if (!count) throw std::runtime_error("no RGBA8 GLES3 pbuffer configuration");
      const EGLint size[] = {EGL_WIDTH, EGLint(width), EGL_HEIGHT, EGLint(height), EGL_NONE};
      surface = eglCreatePbufferSurface(display, config, size);
      egl_check(surface != EGL_NO_SURFACE, "eglCreatePbufferSurface");
      const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
      context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
      egl_check(context != EGL_NO_CONTEXT, "eglCreateContext");
      egl_check(eglMakeCurrent(display, surface, surface, context), "eglMakeCurrent");
    } catch (...) { release(); throw; }
  }
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  ~Context() { release(); }
  void release() {
    if (display != EGL_NO_DISPLAY) {
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
      if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
      eglTerminate(display);
    }
  }
};
GLuint shader(GLenum type, const char* source) {
  const GLuint id = glCreateShader(type);
  glShaderSource(id, 1, &source, nullptr);
  glCompileShader(id);
  GLint ok = 0;
  glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096]{};
    glGetShaderInfoLog(id, sizeof(log), nullptr, log);
    glDeleteShader(id);
    throw std::runtime_error(std::string("shader compilation failed: ") + log);
  }
  return id;
}
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
} // namespace wz3
