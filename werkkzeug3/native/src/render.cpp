// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Headless native Linux graphics path; no X server or Windows runtime required.
#include "wz3/texture.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
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
  Context(unsigned width, unsigned height) {
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
} // namespace wz3
