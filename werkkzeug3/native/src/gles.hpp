// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <sstream>
#include <memory>
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
// A synchronous nested bake must preserve the caller's entire GL state. A
// separate, unshared context provides that isolation without a fragile list of
// bindings to save. Borrow an active display; never terminate that display.
struct ScopedContext {
  EGLDisplay display=eglGetCurrentDisplay();
  EGLContext previous=eglGetCurrentContext(),context=EGL_NO_CONTEXT;
  EGLSurface draw=eglGetCurrentSurface(EGL_DRAW),read=eglGetCurrentSurface(EGL_READ),surface=EGL_NO_SURFACE;
  EGLenum api=eglQueryAPI();
  std::unique_ptr<Context> standalone;
  ScopedContext() {
    try {
      if(previous==EGL_NO_CONTEXT){standalone=std::make_unique<Context>(1,1);return;}
      egl_check(eglBindAPI(EGL_OPENGL_ES_API),"bake API selection");
      const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_SAMPLE_BUFFERS,0,EGL_SAMPLES,0,EGL_NONE};
      EGLConfig config;EGLint count=0;egl_check(eglChooseConfig(display,attributes,&config,1,&count),"bake EGL configuration");
      if(!count)throw std::runtime_error("no RGBA8 GLES3 configuration for bitmap bake");
      const EGLint size[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};surface=eglCreatePbufferSurface(display,config,size);egl_check(surface!=EGL_NO_SURFACE,"bake EGL surface");
      const EGLint version[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);egl_check(context!=EGL_NO_CONTEXT,"bake EGL context");
      egl_check(eglMakeCurrent(display,surface,surface,context),"bake context activation");
    }catch(...){release();throw;}
  }
  ScopedContext(const ScopedContext&)=delete;ScopedContext& operator=(const ScopedContext&)=delete;
  ~ScopedContext(){release();}
  void release() {
    if(standalone){standalone.reset();eglBindAPI(api);return;}
    eglBindAPI(api);
    if(previous!=EGL_NO_CONTEXT) {
      eglMakeCurrent(display,draw,read,previous);
      if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
      if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
      context=EGL_NO_CONTEXT;surface=EGL_NO_SURFACE;
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
