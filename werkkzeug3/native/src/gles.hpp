// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
namespace wz3::gles {
inline void egl_check(bool ok, const char* action) {
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
inline GLuint shader(GLenum type, const char* source) {
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
namespace wz3 {
struct SceneFrame;
// Draw into the current framebuffer and viewport without clearing or reading it.
void draw_scene_geometry(const SceneFrame&,unsigned width,unsigned height);
}
