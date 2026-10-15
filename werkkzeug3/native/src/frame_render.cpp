// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/frame.hpp"
#include "wz3/texture.hpp"
#include "wz3/print.hpp"
#include "wz3/chainline.hpp"
#include "wz3/scene_draw.hpp"
#include "wz3/lighting.hpp"
#include "wz3/fog.hpp"
#include "wz3/material20.hpp"
#include <cmath>
#include "gles.hpp"
#include <algorithm>
#include <iostream>
#include <set>

namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
constexpr const char* quad_vertex=R"(#version 300 es
  uniform vec4 rectangle;
  uniform vec4 uv_rect[4];
  uniform mat3 uv_transform[4];
  uniform vec4 flip_texture;
  uniform float depth;
  uniform vec2 layer_viewport;
  out vec2 material_uv[4];
  out vec4 material_vertex_color;
  void main() {
    vec2 p=vec2(float((gl_VertexID>>1)&1),float(gl_VertexID&1));
    vec2 xy=mix(rectangle.xy,rectangle.zw,p);
    if(layer_viewport.x>0.0) {
      // D3D9 normalized Layer2D covers integer centers with top/left edges
      // inclusive. Snap coverage to pixel boundaries, avoiding GLES's opposite
      // horizontal-edge tie when a half-pixel shift lands on a sample center.
      vec2 pixel=ceil(xy*layer_viewport);
      xy=pixel/layer_viewport;
      // Preserve the original interpolation at integer sample centers. Moving
      // coverage alone would stretch the texture and alter its derivatives.
      p=(pixel-0.5-rectangle.xy*layer_viewport)/((rectangle.zw-rectangle.xy)*layer_viewport);
    }
    // FXQuad already compensates by -0.5 pixels in the original projection;
    // that cancels the D3D-to-GLES sample-center adjustment.
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
constexpr const char* generated_uv_vertex=R"(#version 300 es
  layout(location=0) in vec3 position;
  layout(location=1) in vec2 uv;
  layout(location=2) in vec4 color;
  layout(location=3) in vec3 normal;
  uniform mat4 view_projection,model,model_view;
  uniform mat4 coordinate_transform[4];
  uniform vec4 flip_texture;
  uniform int environment_mode,projection_mode;
  uniform vec4 fog_plane;
  out float material_fog;
  out vec2 material_uv[4];
  out vec4 material_vertex_color;
  void main(){
    vec4 pos=vec4(position,1.0);
    gl_Position=view_projection*pos;
    material_fog=clamp(dot(pos,fog_plane),0.0,1.0);
    for(int i=0;i<4;++i){
      vec4 coord=vec4(uv,0.0,1.0);
      if(i==2){
        if(environment_mode==1){
          vec3 n=mat3(model_view)*normal;
          coord.xy=n.xy*vec2(0.5,-0.5)+0.5;
        }else if(environment_mode==2){
          vec3 eye=normalize((model_view*pos).xyz);
          vec3 n=mat3(model_view)*normal;
          coord.w=dot(eye,n)*(2.0/dot(n,n));
          coord.xyz=n*coord.w-eye;
          coord.xy=coord.xy*vec2(0.5,-0.5)+0.5;
        }else if(environment_mode==3){coord.xyz=normal;}
      }
      if(i==3){
        if(projection_mode==1)coord=pos;
        else if(projection_mode==2)coord=model*pos;
        else if(projection_mode==3)coord=model_view*pos;
      }
      material_uv[i]=(coordinate_transform[i]*coord).xy;
      if(flip_texture[i]>0.5)material_uv[i].y=1.0-material_uv[i].y;
    }
    material_vertex_color=color.bgra;
  })";
// InstancingInner transformed vertices before the ordinary material shader.
// Keep the parent model/light/fog space, and do not normalize transformed axes.
std::string instance_vertex(std::string source) {
  const auto start=source.find('\n');require(start!=std::string::npos,"missing shader version");
  source.insert(start+1,"layout(location=5) in mat4 instance_transform;\n");
  std::string assignments;
  for(const std::string name:{"position","normal","tangent"}) {
    const auto declaration="in vec3 "+name+";";const auto at=source.find(declaration);
    if(at==std::string::npos){require(name!="position","instance shader needs positions");continue;}
    source.replace(at,declaration.size(),"in vec3 source_"+name+"; vec3 "+name+";");
    assignments+=name+"="+(name=="position" ? "(instance_transform*vec4(source_position,1.0)).xyz;" : "mat3(instance_transform)*source_"+name+";");
  }
  const auto main=source.find("void main()");require(main!=std::string::npos,"instance shader needs main");
  source.replace(main,11,"void instance_main()");
  return source+"\nvoid main(){"+assignments+"instance_main();}\n";
}
constexpr const char* shadow_vertex=R"(#version 300 es
  layout(location=0) in vec4 position;
  uniform mat4 view_projection;
  uniform vec3 light_position;
  void main() {
    // material11.vsh: finite vertex w=1; extruded direction w=0.
    gl_Position=view_projection*vec4(position.xyz+light_position*(position.w-1.0),position.w);
  })";
constexpr const char* glare_vertex=R"(#version 300 es
  out vec2 uv;
  void main(){uv=vec2(float((gl_VertexID>>1)&1),float(gl_VertexID&1));gl_Position=vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,0.0,1.0);}
)";
constexpr const char* glare_fragment=R"(#version 300 es
  precision highp float;
  uniform sampler2D input_image,original_image;
  uniform int mode;
  uniform vec2 input_scale,original_scale;
  uniform vec3 taps[9];
  uniform vec4 constants[3];
  in vec2 uv;
  out vec4 color;
  vec4 sample_input(vec2 p){return texture(input_image,vec2(p.x,1.0-p.y));}
  void main(){
    vec2 p=uv*input_scale;
    if(mode==0){
      vec4 acc=constants[0];
      for(int i=0;i<9;++i)acc=sample_input(p+taps[i].xy)*taps[i].z+acc;
      color=clamp(acc,0.0,1.0);
    }else if(mode==1){color=sample_input(p);}
    else if(mode==2){
      vec4 col=sample_input(p);col.rgb-=constants[0].rgb;
      float gray=dot(col.rgb,vec3(constants[0].w));
      color=vec4(gray)*constants[1].y+col*constants[1].x;
    }else{
      vec4 a=sample_input(p)*constants[0];
      vec2 q=uv*original_scale;
      vec4 b=texture(original_image,vec2(q.x,1.0-q.y))*constants[1];
      color=(a-a*b*constants[2].x)+b;
    }
  }
)";
constexpr const char* color_correction_fragment=R"(#version 300 es
  precision highp float;
  uniform sampler2D input_image;
  uniform vec2 input_scale;
  uniform vec4 constants[7];
  in vec2 uv;
  out vec4 color;
  void main(){
    vec2 p=uv*input_scale;
    vec4 accu=texture(input_image,vec2(p.x,1.0-p.y));
    float gray=dot(accu.rgb,vec3(0.333333333));
    vec3 dark=accu.rgb*constants[0].rgb+gray*constants[5].rgb;
    float blend=clamp(gray*constants[4].y+constants[4].x,0.0,1.0);
    vec3 light=accu.rgb*constants[1].rgb+gray*constants[6].rgb;
    vec3 mixed=light*(1.0-blend)+dark*blend;
    mixed+=constants[4].z*blend;
    // The saved "gamma" parameter is a saturated subtraction, not a power.
    color=vec4(clamp(mixed-constants[3].rgb,0.0,1.0)*constants[2].rgb,accu.a);
  }
)";
void clear(FrameClear c,bool stencil=false) {
  glDepthMask(GL_TRUE);glClearDepthf(1);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glClearColor(((c.color>>16)&255)/255.0f,((c.color>>8)&255)/255.0f,(c.color&255)/255.0f,(c.color>>24)/255.0f);
  if(stencil && (c.flags&2)) {glStencilMask(255);glClearStencil(0);}
  glClear(((c.flags&1) ? GL_COLOR_BUFFER_BIT : 0)|((c.flags&2) ? GL_DEPTH_BUFFER_BIT|(stencil ? GL_STENCIL_BUFFER_BIT : 0) : 0));
}
}
namespace {
class BitmapRenderCache final : public RenderedBitmapProvider,public std::enable_shared_from_this<BitmapRenderCache> {
  const Project& project;const Graph& graph;TextureOptions options;
  using Key=std::tuple<std::size_t,std::size_t,unsigned,unsigned>;
  std::map<Key,std::shared_ptr<const Texture>> cache;std::set<std::size_t> pending;std::size_t bytes{},reserved{};
public:
  BitmapRenderCache(const Project& p,const Graph& g,TextureOptions o):project(p),graph(g),options(std::move(o)){options.rendered_bitmaps.reset();}
  Stats stats()const override{return {cache.size(),bytes};}
  std::shared_ptr<const Texture> get(std::size_t id,std::size_t input,unsigned width,unsigned height)override {
    const Key key{id,input,width,height};if(auto found=cache.find(key);found!=cache.end())return found->second;
    require(id<project.operators.size() && input<project.operators.size() && width && height && width<=4096 && height<=4096,"invalid Render bitmap request");
    require(pending.size()<16 && !pending.contains(id),"recursive Render bitmap dependency");
    const auto size=std::size_t(width)*height*sizeof(Pixel);require(size<=128u*1024*1024-bytes-reserved,"rendered bitmap cache budget exceeded");
    pending.insert(id);reserved+=size;
    struct Remove {BitmapRenderCache& owner;std::size_t id,size;~Remove(){owner.pending.erase(id);owner.reserved-=size;}} remove{*this,id,size};
    auto nested_options=options;nested_options.rendered_bitmaps=shared_from_this();
    gles::ScopedContext context;
    FramePlayer player(project,graph,nested_options);auto frame=player.bitmap_frame(input,width,height);
    frame.commands.insert(frame.commands.begin(),FrameCommand{id,{0,width,height},FrameClear{3,0xffff8080}});
    FrameRenderer renderer(project,graph,nested_options,FrameRenderer::Context::Current);
    auto texture=std::make_shared<Texture>(bitmap_readback(width,height,renderer.render(frame)));
    cache.emplace(key,texture);bytes+=size;return texture;
  }
};
}
TextureOptions rendered_texture_options(const Project& p,const Graph& g,TextureOptions options) {
  if(!options.rendered_bitmaps)options.rendered_bitmaps=std::make_shared<BitmapRenderCache>(p,g,options);
  return options;
}
struct FrameRenderer::Impl {
  const Project& project;const Graph& graph;TextureOptions texture_options;std::unique_ptr<gles::Context> context;
  struct Surface {GLuint color{},depth{},fbo{};unsigned width{},height{};};
  std::map<std::size_t,Surface> surfaces;
  Surface shared_depth,albedo;
  std::array<Surface,3> glare_images;
  std::array<Surface,2> glare_scratch;
  unsigned capture_width{},capture_height{};
  std::map<std::size_t,GLuint> bitmaps;
  std::map<std::size_t,std::shared_ptr<const Texture>> supplied_bitmaps;
  std::map<std::size_t,std::size_t> bitmap_sizes;
  std::map<std::string,GLuint> programs;
  struct MeshGeometry {
    std::shared_ptr<const Mesh> owner;GLuint buffer{};
    std::size_t bytes{},reserved{};
    std::vector<std::pair<GLint,GLsizei>> batches;
    std::vector<std::size_t> instance_limits;
  };
  struct MeshVertex {SceneEffectVertex vertex;Vec3 normal,tangent;};
  struct MeshDraw {GLuint buffer;GLint first;GLsizei count;SceneMatrix model;bool instanced{};GLuint instance_buffer{};GLsizei instance_count{};};
  struct MeshInstances {
    std::shared_ptr<const Mesh> owner;
    std::shared_ptr<const std::vector<SceneMatrix>> matrices;
    GLuint buffer{};std::size_t bytes{},reserved{};
  };
  std::map<const Mesh*,MeshInstances> instance_buffers;
  SceneMatrix last_model=scene_identity();
  struct LightDraw {Vec3 position,eye;std::array<float,4> attenuation,color;std::array<float,2> specular;bool directional;};
  struct ShadowGeometry {
    std::shared_ptr<const Mesh> owner;std::vector<std::uint8_t> mask;
    ShadowMesh mesh;GLuint buffer{};std::size_t bytes{},reserved{};
  };
  std::map<const Mesh*,ShadowGeometry> shadows;
  GLuint shadow_program{},shadow_index_buffer{};
  GLuint normal_cube{},attenuation_volume{};
  std::map<const Mesh*,MeshGeometry> geometry;
  std::size_t geometry_bytes{};
  GLuint vao{},particle_buffer{};std::array<GLuint,4> samplers{};
  std::size_t surface_bytes{},bitmap_bytes{};
  FrameRenderer::Stats stats;
  Impl(const Project& p,const Graph& g,const TextureOptions& options,FrameRenderer::Context mode):project(p),graph(g),texture_options(rendered_texture_options(p,g,options)) {
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
    destroy(albedo);
    for(const auto& s:glare_images)destroy(s);
    for(const auto& s:glare_scratch)destroy(s);
    for(const auto& [id,t]:bitmaps) {static_cast<void>(id);glDeleteTextures(1,&t);}
    for(const auto& [source,p]:programs) {static_cast<void>(source);glDeleteProgram(p);}
    for(const auto& [id,g]:geometry) {static_cast<void>(id);glDeleteBuffers(1,&g.buffer);}
    for(const auto& [id,g]:shadows) {static_cast<void>(id);glDeleteBuffers(1,&g.buffer);}
    for(const auto& [id,g]:instance_buffers) {static_cast<void>(id);glDeleteBuffers(1,&g.buffer);}
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
  void albedo_surface(const FrameTarget& target) {
    // RenderTargetManager's 0x00010000 follows the master viewport and never
    // shrinks. Keep its color storage separate from graph render target IDs.
    const auto width=std::max(albedo.width,target.width),height=std::max(albedo.height,target.height);
    if(width==albedo.width && height==albedo.height)return;
    surface_bytes-=std::size_t(albedo.width)*albedo.height*4;destroy(albedo);albedo={};
    const auto bytes=std::size_t(width)*height*4;require(bytes<=256u*1024*1024-surface_bytes,"Material 2.0 albedo target budget exceeded");surface_bytes+=bytes;
    albedo.width=width;albedo.height=height;glGenTextures(1,&albedo.color);glBindTexture(GL_TEXTURE_2D,albedo.color);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,width,height);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    glGenFramebuffers(1,&albedo.fbo);glBindFramebuffer(GL_FRAMEBUFFER,albedo.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,albedo.color,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"incomplete Material 2.0 albedo target");
    glDisable(GL_SCISSOR_TEST);clear({1,0xff000000});glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,surfaces.at(target.surface).fbo);++stats.surface_allocations;
  }
  void capture_albedo(const FrameTarget& target) {
    const auto& from=surfaces.at(target.surface);
    glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,from.fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,albedo.fbo);
    glBlitFramebuffer(0,from.height-target.height,target.width,from.height,
      0,albedo.height-target.height,target.width,albedo.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,from.fbo);glEnable(GL_SCISSOR_TEST);
  }
  void glare_surface(Surface& s,unsigned width,unsigned height) {
    if(s.width==width && s.height==height)return;
    surface_bytes-=std::size_t(s.width)*s.height*4;destroy(s);s={};
    const auto bytes=std::size_t(width)*height*4;require(bytes<=256u*1024*1024-surface_bytes,"Glare target budget exceeded");surface_bytes+=bytes;
    s.width=width;s.height=height;glGenTextures(1,&s.color);glBindTexture(GL_TEXTURE_2D,s.color);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,width,height);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    glGenFramebuffers(1,&s.fbo);glBindFramebuffer(GL_FRAMEBUFFER,s.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.color,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"incomplete Glare surface");++stats.surface_allocations;
  }
  void capture_current(const FrameTarget& target,bool clear_capture=false) {
    const auto& output=surfaces.at(target.surface);
    // Shared RenderTargetManager target zero: Glare overwrites the same image
    // that Color Correction reads after the engine's pass-boundary capture.
    glare_surface(glare_images[0],capture_width,capture_height);
    glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,glare_images[0].fbo);
    if(clear_capture)clear({1,0xff000000});
    glBindFramebuffer(GL_READ_FRAMEBUFFER,output.fbo);
    glBlitFramebuffer(0,output.height-target.height,target.width,output.height,0,capture_height-target.height,target.width,capture_height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,output.fbo);glEnable(GL_SCISSOR_TEST);
  }
  void color_correction(const ColorCorrectionEffect& effect,const FrameTarget& target) {
    const auto constants=color_correction_constants(effect.parameters);const auto& output=surfaces.at(target.surface);
    require(glare_images[0].color && capture_width>=target.width && capture_height>=target.height,"Color Correction requires a captured render pass");
    GLuint program;
    if(const auto found=programs.find("color_correction");found!=programs.end())program=found->second;
    else {
      const auto vertex=gles::shader(GL_VERTEX_SHADER,glare_vertex),fragment=gles::shader(GL_FRAGMENT_SHADER,color_correction_fragment);
      program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
      GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(program);throw std::runtime_error("Color Correction shader link failed");}
      programs.emplace("color_correction",program);++stats.program_compilations;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,output.fbo);glViewport(0,output.height-target.height,target.width,target.height);
    glUseProgram(program);glBindVertexArray(vao);glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUniform1i(glGetUniformLocation(program,"input_image"),0);glUniform2f(glGetUniformLocation(program,"input_scale"),float(target.width)/capture_width,float(target.height)/capture_height);
    glUniform4fv(glGetUniformLocation(program,"constants"),7,constants[0].data());
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,glare_images[0].color);glBindSampler(0,samplers[0]);
    glSamplerParameteri(samplers[0],GL_TEXTURE_MIN_FILTER,GL_LINEAR);glSamplerParameteri(samplers[0],GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glSamplerParameteri(samplers[0],GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glSamplerParameteri(samplers[0],GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glEnable(GL_SCISSOR_TEST);glScissor(0,output.height-target.height,target.width,target.height);
  }
  void glare(const GlareEffect& effect,const FrameTarget& target) {
    const auto passes=glare_passes(effect.parameters,target.width,target.height,capture_width,capture_height);
    const auto& output=surfaces.at(target.surface);capture_current(target,true);glDisable(GL_SCISSOR_TEST);
    GLuint program;
    if(const auto found=programs.find("glare");found!=programs.end())program=found->second;
    else {
      const auto vertex=gles::shader(GL_VERTEX_SHADER,glare_vertex),fragment=gles::shader(GL_FRAGMENT_SHADER,glare_fragment);
      program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
      GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(program);throw std::runtime_error("Glare shader link failed");}
      programs.emplace("glare",program);++stats.program_compilations;
    }
    glUseProgram(program);glBindVertexArray(vao);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUniform1i(glGetUniformLocation(program,"input_image"),0);glUniform1i(glGetUniformLocation(program,"original_image"),1);
    for(unsigned i=0;i<2;++i) {
      glBindSampler(i,samplers[i]);glSamplerParameteri(samplers[i],GL_TEXTURE_MIN_FILTER,GL_LINEAR);glSamplerParameteri(samplers[i],GL_TEXTURE_MAG_FILTER,GL_LINEAR);
      glSamplerParameteri(samplers[i],GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glSamplerParameteri(samplers[i],GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    for(const auto& pass:passes) {
      if(pass.target!=3)glare_surface(glare_scratch[pass.target-1],pass.width,pass.height);
      const auto& dest=pass.target==3 ? output : glare_scratch[pass.target-1];
      glBindFramebuffer(GL_FRAMEBUFFER,dest.fbo);glViewport(0,dest.height-pass.height,pass.width,pass.height);
      glUniform1i(glGetUniformLocation(program,"mode"),pass.mode);glUniform2fv(glGetUniformLocation(program,"input_scale"),1,pass.uv.data());
      glUniform2fv(glGetUniformLocation(program,"original_scale"),1,pass.original_uv.data());glUniform3fv(glGetUniformLocation(program,"taps"),9,pass.taps[0].data());
      glUniform4fv(glGetUniformLocation(program,"constants"),3,pass.constants[0].data());
      glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,glare_images[pass.source].color);
      glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,glare_images[0].color);
      glDrawArrays(GL_TRIANGLE_STRIP,0,4);
      if(pass.target!=3)std::swap(glare_images[pass.target],glare_scratch[pass.target-1]);
      if(pass.mode==GlarePass::copy)last_model=scene_identity(); // GENOVER_TEX1 is a Material11 draw.
    }
    glEnable(GL_SCISSOR_TEST);glScissor(0,output.height-target.height,target.width,target.height);
  }
  GLuint bitmap(std::size_t id,std::shared_ptr<const Texture> supplied={}) {
    const auto found=bitmaps.find(id);
    if(found!=bitmaps.end() && (!supplied || (supplied_bitmaps.contains(id) && supplied_bitmaps.at(id)==supplied)))return found->second;
    const auto t=supplied ? supplied : evaluate_texture(project,graph,id,texture_options);
    const auto previous=bitmap_sizes.contains(id) ? bitmap_sizes.at(id) : 0;
    const bool dxt=t->format==8 || t->format==9;
    if(dxt) {
      bool supported=false;GLint extensions=0;glGetIntegerv(GL_NUM_EXTENSIONS,&extensions);
      for(GLint i=0;i<extensions;++i) {
        const std::string_view extension(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS,i)));
        supported|=extension=="GL_EXT_texture_compression_s3tc" ||
          extension==(t->format==8 ? "GL_EXT_texture_compression_dxt1" : "GL_ANGLE_texture_compression_dxt5");
      }
      require(supported,"saved DXT texture requires S3TC compression support from the GLES driver");
    }
    const auto levels=t->format==5 ? signed_normal_levels(*t,project.runtime_graph) : std::vector<SignedNormalLevel>{};
    const auto colors=t->format==5 || dxt ? std::vector<ColorTextureLevel>{} : color_texture_levels(*t);
    const auto compressed=dxt ? compressed_texture_levels(*t) : std::vector<CompressedTextureLevel>{};
    std::size_t bytes=0;for(const auto& level:colors)bytes+=level.pixels.size();for(const auto& level:levels)bytes+=level.pixels.size();for(const auto& level:compressed)bytes+=level.blocks.size();
    // Retain the complete demo's static uploads across loading and playback.
    // Debris exceeds 256 MiB when its final city sequence starts.
    constexpr std::size_t bitmap_limit=512u*1024*1024;
    if(bytes>bitmap_limit-(bitmap_bytes-previous))throw std::runtime_error("frame bitmap budget exceeded: texture "+std::to_string(id)+
      ", retained "+std::to_string(bitmap_bytes-previous)+", requested "+std::to_string(bytes)+", limit "+std::to_string(bitmap_limit)+" bytes");
    GLuint texture=0;if(found!=bitmaps.end())texture=found->second;else glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    if(dxt) {
      // GL_COMPRESSED_RGB_S3TC_DXT1_EXT / GL_COMPRESSED_RGBA_S3TC_DXT5_EXT.
      const GLenum format=t->format==8 ? 0x83f0 : 0x83f3;
      for(std::size_t i=0;i<compressed.size();++i){const auto& level=compressed[i];glCompressedTexImage2D(GL_TEXTURE_2D,GLint(i),format,level.width,level.height,0,level.blocks.size(),level.blocks.data());}
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,GLint(compressed.size()-1));
    }
    else if(levels.empty()){for(std::size_t i=0;i<colors.size();++i){const auto& level=colors[i];glTexImage2D(GL_TEXTURE_2D,GLint(i),GL_RGBA8,level.width,level.height,0,GL_RGBA,GL_UNSIGNED_BYTE,level.pixels.data());}glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,GLint(colors.size()-1));}
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
  GLuint program(const MaterialShader& s,bool particle,bool mesh=false,bool instanced=false) {
    const bool lit=(s.words[2]&7)!=0;require(!lit || particle,"lighting needs mesh vertex data");
    const bool generated=(s.words[1]&0xf30)!=0;
    require(!generated || (mesh && !lit),"generated environment/projection UVs need an unlit mesh shader");
    auto source=lit ? material_light_fragment_shader(s) : material_fragment_shader(s);if(s.words[0]&0x40)source=fog_fragment_shader(std::move(source));
    const auto key=std::string(instanced ? "instanced:" : "")+(generated ? "generated:" : lit ? "light:" : particle ? "particle:" : "quad:")+source;if(auto found=programs.find(key);found!=programs.end())return found->second;
    require(programs.size()<4096,"frame shader cache limit");
    std::string vertex_source=generated ? generated_uv_vertex : lit ? light_vertex : particle ? particle_vertex : quad_vertex;
    if(instanced)vertex_source=instance_vertex(std::move(vertex_source));
    const auto vertex=gles::shader(GL_VERTEX_SHADER,vertex_source.c_str());GLuint fragment;
    try {fragment=gles::shader(GL_FRAGMENT_SHADER,source.c_str());}catch(...) {glDeleteShader(vertex);throw;}
    const auto p=glCreateProgram();glAttachShader(p,vertex);glAttachShader(p,fragment);glLinkProgram(p);glDeleteShader(vertex);glDeleteShader(fragment);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("frame shader link failed: ")+log);}
    programs.emplace(key,p);++stats.program_compilations;return p;
  }
  void quad(const FrameQuad& q,const FrameTarget& target,const SceneEffectDraw* effect=nullptr,const SceneMatrix* projection=nullptr,const MeshDraw* mesh=nullptr,const LightDraw* light=nullptr,const SceneFog* fog=nullptr,const SceneCamera* camera=nullptr) {
    const auto destination=target.surface;
    if(q.released_layer && !effect && !mesh &&
       (q.rectangle[0]==q.rectangle[2] || q.rectangle[1]==q.rectangle[3]))return;
    const auto& s=q.shader;const auto flags=s.words[0];
    require(!(flags&(light ? 0xcc0018u : 0xfc0018u)),"draw shadow state or depth bias is not ported");
    require(!(flags&0x700000) || (light && (flags&0x700000)==0x300000),"unsupported stencil material operation");
    const bool vertices=effect || mesh;require(!(s.words[2]&7) || light,"missing scene light uniforms");require(!(flags&0x40) || (vertices && fog),"fog requires a scene camera");const auto p=program(s,vertices,mesh!=nullptr,mesh && mesh->instanced);glUseProgram(p);glBindVertexArray(vao);
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
    glUniform2f(glGetUniformLocation(p,"layer_viewport"),q.released_layer ? target.width : 0,q.released_layer ? target.height : 0);
    glUniform4fv(glGetUniformLocation(p,"uv_rect[0]"),4,rects.data());glUniformMatrix3fv(glGetUniformLocation(p,"uv_transform[0]"),4,GL_FALSE,matrices.data());
    glUniform4fv(glGetUniformLocation(p,"flip_texture"),1,flip.data());glUniform4fv(glGetUniformLocation(p,"material_colors[0]"),4,colors.data());
    if(s.words[1]&0xf30) {
      require(mesh && camera,"generated UVs require model and camera transforms");
      require(!(s.words[3]&0xc0000),"generated UVs with shader instancing are not ported");
      require(!(s.words[1]&0x30) || !(flags&0x20000),"environment UVs require mesh normals");
      auto view=scene_identity();for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)view[c*4+r]=camera->world[r*4+c];
      for(unsigned r=0;r<3;++r)view[12+r]=-(view[r]*camera->world[12]+view[4+r]*camera->world[13]+view[8+r]*camera->world[14]);
      const auto model_view=scene_combine(view,mesh->model);
      glUniformMatrix4fv(glGetUniformLocation(p,"model"),1,GL_FALSE,mesh->model.data());glUniformMatrix4fv(glGetUniformLocation(p,"model_view"),1,GL_FALSE,model_view.data());
      glUniform1i(glGetUniformLocation(p,"environment_mode"),(s.words[1]>>4)&3);glUniform1i(glGetUniformLocation(p,"projection_mode"),(s.words[1]>>8)&15);
      std::array<float,64> transforms;
      for(unsigned i=0;i<4;++i){const auto transform=material_uv_transform(s,i);std::copy(transform.begin(),transform.end(),transforms.begin()+i*16);}
      glUniformMatrix4fv(glGetUniformLocation(p,"coordinate_transform"),4,GL_FALSE,transforms.data());
    }
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
    last_model=mesh ? mesh->model : effect ? effect->material_model : scene_identity();
    if(!vertices)glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    else {
      if(mesh && mesh->instanced && !mesh->instance_count)return;
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
      if(mesh && mesh->instanced) {
        glBindBuffer(GL_ARRAY_BUFFER,mesh->instance_buffer);
        for(unsigned c=0;c<4;++c){glVertexAttribPointer(5+c,4,GL_FLOAT,GL_FALSE,sizeof(SceneMatrix),reinterpret_cast<void*>(c*4*sizeof(float)));glEnableVertexAttribArray(5+c);glVertexAttribDivisor(5+c,1);}
        glDrawArraysInstanced(GL_TRIANGLES,first,count,mesh->instance_count);
        for(unsigned c=0;c<4;++c){glVertexAttribDivisor(5+c,0);glDisableVertexAttribArray(5+c);}
      }else glDrawArrays(GL_TRIANGLES,first,count);
    }
  }
  FrameTexture resolve_texture(std::size_t id,const SceneEffectDraw* effect=nullptr) {
      if(effect && effect->textures.contains(id))return FrameTexture{false,id};
      bool resolved=false;
      for(unsigned depth=0;depth<512;++depth) {
        require(id<project.operators.size() && graph.nodes[id].active,"invalid scene texture");const auto cmd=project.operators[id].command;
        if(cmd==1) {require(graph.nodes[id].links.size()==1 && graph.nodes[id].links[0],"unresolved scene texture Load");id=*graph.nodes[id].links[0];}
        else if(cmd==2 || cmd==3) {require(graph.nodes[id].inputs.size()==1,"invalid scene texture alias");id=graph.nodes[id].inputs[0];}
        else {resolved=true;break;}
      }
      require(resolved,"scene texture alias depth limit");
      return project.operators[id].command==0x26 ? FrameTexture{true,13+id} : FrameTexture{false,id};
  }
  FrameQuad resolve_material(const MaterialShader& shader,const SceneEffectDraw* effect=nullptr) {
    require(!shader.material20,"Material 2.0 requires a mesh draw");
    FrameQuad q;q.shader=shader;
    for(unsigned slot=0;slot<4;++slot)if(q.shader.textures[slot])q.textures[slot]=resolve_texture(*q.shader.textures[slot],effect);
    return q;
  }
  void material20(const Material20Shader& shader,const FrameTarget& target,const MeshDraw& mesh,const SceneMatrix& projection,const SceneFrame& scene,const SceneLight* light) {
    const auto source=material20_source(shader);const auto key=source.vertex+source.fragment;
    GLuint p;
    if(const auto found=programs.find(key);found!=programs.end())p=found->second;
    else {
      require(programs.size()<4096,"frame shader cache limit");
      const auto vertex=gles::shader(GL_VERTEX_SHADER,source.vertex.c_str());GLuint fragment;
      try {fragment=gles::shader(GL_FRAGMENT_SHADER,source.fragment.c_str());}catch(...){glDeleteShader(vertex);throw;}
      p=glCreateProgram();glAttachShader(p,vertex);glAttachShader(p,fragment);glLinkProgram(p);glDeleteShader(vertex);glDeleteShader(fragment);
      GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
      if(!ok){char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("Material 2.0 shader link failed: ")+log);}
      programs.emplace(key,p);++stats.program_compilations;
    }
    glUseProgram(p);glBindVertexArray(vao);
    std::array<float,27> matrices{};std::array<float,3> flip{};
    for(std::size_t i=0;i<source.samplers.size();++i) {
      const auto slot=source.samplers[i];GLuint texture;glActiveTexture(GL_TEXTURE0+i);
      if(slot==7){require(albedo.color,"missing Material 2.0 albedo capture");texture=albedo.color;}
      else {
        const auto binding=resolve_texture(*shader.textures[slot]);
        if(binding.surface) {
          require(binding.id!=target.surface,"Material 2.0 render target feedback is not supported");
          require(surfaces.contains(binding.id),"missing Material 2.0 sampled surface");texture=surfaces.at(binding.id).color;flip[i]=1;
        }else texture=bitmap(binding.id);
        const auto matrix=material20_uv_matrix(shader,slot);std::copy(matrix.begin(),matrix.end(),matrices.begin()+i*9);
      }
      const auto flags=material20_sampler_flags(shader,slot),filter=flags&7,sampler=samplers[i];
      constexpr GLint min_filter[]={GL_NEAREST,GL_LINEAR,GL_LINEAR_MIPMAP_NEAREST,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR,GL_NEAREST,GL_NEAREST,GL_NEAREST};
      glBindTexture(GL_TEXTURE_2D,texture);glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,min_filter[filter]);
      glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,filter>=1 && filter<=4 ? GL_LINEAR : GL_NEAREST);
      glSamplerParameteri(sampler,GL_TEXTURE_WRAP_S,flags&0x100 ? GL_CLAMP_TO_EDGE : GL_REPEAT);glSamplerParameteri(sampler,GL_TEXTURE_WRAP_T,flags&0x100 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
      const auto extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
      const bool anisotropic=extensions && std::string_view(extensions).find("GL_EXT_texture_filter_anisotropic")!=std::string_view::npos;
      require(filter!=4 || anisotropic,"Material 2.0 needs anisotropic filtering");
      if(anisotropic){GLfloat maximum=1;glGetFloatv(0x84ff,&maximum);glSamplerParameterf(sampler,0x84fe,filter==4 ? std::min(4.f,maximum) : 1.f);}
      glBindSampler(GLuint(i),sampler);glUniform1i(glGetUniformLocation(p,("material_tex"+std::to_string(slot)).c_str()),GLint(i));
      if(slot==6)glUniform1f(glGetUniformLocation(p,"environment_flip"),flip[i]);
    }
    glUniformMatrix3fv(glGetUniformLocation(p,"uv_transform[0]"),3,GL_FALSE,matrices.data());glUniform3fv(glGetUniformLocation(p,"flip_texture"),1,flip.data());
    glUniformMatrix4fv(glGetUniformLocation(p,"view_projection"),1,GL_FALSE,projection.data());
    const auto stage=shader.stage;
    if(stage==Material20Stage::depth)glUniform1f(glGetUniformLocation(p,"alpha_burn"),as_float(shader.parameters[4]));
    if(stage==Material20Stage::texture && (shader.compiled_parameters[0]&0x200)) {
      const auto m=material20_color_matrix(shader);glUniform4fv(glGetUniformLocation(p,"color_matrix[0]"),3,m.data());
    }
    if(stage==Material20Stage::ambient) {
      const auto c=scene.ambient;glUniform4f(glGetUniformLocation(p,"ambient"),((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f,(c>>24)/255.f);
    }
    if(stage==Material20Stage::light) {
      require(light,"Material 2.0 light pass needs a scene light");const auto l=material20_lighting(shader,mesh.model,scene.camera,*light);
      glUniform3fv(glGetUniformLocation(p,"light_position"),1,l.position.data());glUniform3fv(glGetUniformLocation(p,"eye_position"),1,l.eye.data());
      glUniform4fv(glGetUniformLocation(p,"attenuation_parameters"),1,l.attenuation.data());glUniform1i(glGetUniformLocation(p,"directional"),l.directional);
      glUniform4fv(glGetUniformLocation(p,"diffuse"),1,l.diffuse.data());glUniform4fv(glGetUniformLocation(p,"specular"),1,l.specular.data());
      const float u=float(target.width)/albedo.width,v=float(target.height)/albedo.height;
      // Original projected lookup adds half a D3D pixel. Convert integer D3D
      // sample centers to GLES half-integers first; preserve the original
      // viewport-sized offset when the retained capture texture is larger.
      glUniform4f(glGetUniformLocation(p,"albedo_projection"),.5f*u,-.5f*v,
        .5f*u+.5f/target.width-.5f/albedo.width,.5f*v+.5f/target.height-.5f/albedo.height);
    }
    if(stage==Material20Stage::environment) {
      const auto m=material20_environment_matrix(shader,mesh.model,scene.camera);
      const auto eye=light_model_vector(mesh.model,{scene.camera.world[12],scene.camera.world[13],scene.camera.world[14]},true);
      glUniformMatrix4fv(glGetUniformLocation(p,"environment_matrix"),1,GL_FALSE,m.data());glUniform3fv(glGetUniformLocation(p,"eye_position"),1,eye.data());
    }
    const bool depth=stage==Material20Stage::depth,lit=stage==Material20Stage::light;
    glColorMask(!depth,!depth,!depth,!depth);glEnable(GL_DEPTH_TEST);glDepthMask(depth);glDepthFunc(depth ? GL_LEQUAL : GL_EQUAL);
    glEnable(GL_CULL_FACE);glFrontFace(GL_CW);glCullFace(GL_BACK);
    if(lit){glEnable(GL_STENCIL_TEST);glStencilFunc(GL_EQUAL,0,255);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);}else glDisable(GL_STENCIL_TEST);
    if(lit || stage==Material20Stage::ambient || stage==Material20Stage::environment) {
      glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
      if(stage==Material20Stage::ambient)glBlendFunc(GL_DST_COLOR,GL_ZERO);
      else glBlendFunc(GL_ONE,lit && (shader.compiled_parameters[0]&4) ? GL_ONE_MINUS_SRC_COLOR : GL_ONE);
    }else glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER,mesh.buffer);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(MeshVertex),reinterpret_cast<void*>(offsetof(SceneEffectVertex,position)));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(MeshVertex),reinterpret_cast<void*>(offsetof(SceneEffectVertex,uv)));
    glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(MeshVertex),reinterpret_cast<void*>(offsetof(SceneEffectVertex,color)));
    glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(MeshVertex),reinterpret_cast<void*>(offsetof(MeshVertex,normal)));
    glVertexAttribPointer(4,3,GL_FLOAT,GL_FALSE,sizeof(MeshVertex),reinterpret_cast<void*>(offsetof(MeshVertex,tangent)));
    for(unsigned i=0;i<5;++i)glEnableVertexAttribArray(i);
    // ZFill::Set does not publish LastMatrix; all four color passes do.
    if(!depth)last_model=mesh.model;
    glDrawArrays(GL_TRIANGLES,mesh.first,mesh.count);
  }
  void effect(const SceneEffectDraw& draw,const SceneMatrix& projection,const FrameTarget& target,const SceneCamera& camera) {
    if(draw.glare){glare(*draw.glare,target);return;}
    if(draw.color_correction){color_correction(*draw.color_correction,target);return;}
    if(draw.vertices.empty() && !draw.ribbon) {if(draw.material_on_empty)last_model=draw.material_model;return;}
    std::optional<SceneEffectDraw> resolved;const auto* geometry=&draw;
    if(draw.ribbon) {
      // GetTransform(MODELVIEW) uses the previous material's model matrix.
      // Only its rotational part contributes to the ribbon's facing vector.
      auto view=scene_identity();for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)view[c*4+r]=camera.world[r*4+c];
      resolved=draw;resolved->vertices=chainline_vertices(*draw.ribbon,scene_combine(view,last_model));geometry=&*resolved;
    }
    auto matrix=draw.normalized ? print_normalized_projection(target.width,target.height) : projection;
    auto draw_camera=camera;
    if(draw.camera_world && !draw.normalized) {draw_camera.world=*draw.camera_world;matrix=scene_raster_projection(draw_camera,target.width,target.height);}
    std::optional<SceneFog> fog;if(draw.shader.words[0]&0x40){require(!draw.normalized,"normalized text fog is not ported");fog=scene_fog(draw_camera,scene_identity());}
    quad(resolve_material(draw.shader,&draw),target,geometry,&matrix,nullptr,nullptr,fog ? &*fog : nullptr);
  }
  void effects(const SceneFrame& scene,const FrameTarget& target) {
    const auto projection=scene_raster_projection(scene.camera,target.width,target.height);
    std::array<bool,256> capture{};for(const auto& draw:scene.effects)if(draw.color_correction)capture[draw.sort_key>>24]=true;
    unsigned previous=256;
    for(const auto& draw:scene.effects){const auto pass=draw.sort_key>>24;if(pass!=previous && capture[pass])capture_current(target);previous=pass;effect(draw,projection,target,scene.camera);}
  }
  const MeshGeometry& mesh_geometry(const std::shared_ptr<const Mesh>& mesh,const std::vector<SceneMeshBatch>& batches) {
    const auto key=mesh->animation_source ? mesh->animation_source.get() : mesh.get();auto found=geometry.find(key);
    if(found!=geometry.end() && found->second.owner==mesh)return found->second;
    MeshGeometry data;data.owner=mesh;std::vector<MeshVertex> triangles;const auto normals=mesh_normals(*mesh);
    std::size_t count=0;for(const auto& batch:batches)for(auto f:batch.faces)count+=(mesh->faces[f].count-2)*3;
    const auto previous=found==geometry.end() ? 0 : found->second.bytes;
    const auto bytes=count*sizeof(MeshVertex);require(bytes<=512u*1024*1024-(geometry_bytes-previous),"scene GPU geometry budget exceeded");triangles.reserve(count);data.bytes=bytes;
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
      data.instance_limits.push_back(scene_instance_limit(*mesh,batch));
    }
    if(found==geometry.end()){glGenBuffers(1,&data.buffer);++stats.mesh_uploads;}
    else {data.buffer=found->second.buffer;++stats.mesh_updates;}
    glBindBuffer(GL_ARRAY_BUFFER,data.buffer);
    if(previous==bytes && found!=geometry.end())glBufferSubData(GL_ARRAY_BUFFER,0,bytes,triangles.data());
    else glBufferData(GL_ARRAY_BUFFER,bytes,triangles.data(),mesh->animation_source ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    geometry_bytes=geometry_bytes-previous+bytes;
    if(found!=geometry.end()){found->second=std::move(data);return found->second;}
    return geometry.emplace(key,std::move(data)).first->second;
  }
  GLuint instance_buffer(const SceneInstance& instance) {
    const auto& matrices=instance.particle_matrices;require(matrices && !matrices->empty() && matrices->size()<=16384,"invalid mesh instance array");
    const auto owner=instance.mesh->animation_source ? instance.mesh->animation_source : instance.mesh;
    auto& data=instance_buffers[owner.get()];if(data.matrices==matrices)return data.buffer;
    for(const auto& m:*matrices){for(auto x:m)require(std::isfinite(x),"nonfinite instance matrix");require(m[3]==0 && m[7]==0 && m[11]==0 && m[15]==1,"nonaffine instance matrix");}
    const auto bytes=matrices->size()*sizeof(SceneMatrix);
    if(!data.buffer){glGenBuffers(1,&data.buffer);data.owner=owner;++stats.instance_uploads;}
    glBindBuffer(GL_ARRAY_BUFFER,data.buffer);
    if(bytes>data.bytes) {
      require(bytes-data.bytes<=512u*1024*1024-geometry_bytes,"scene instance buffer budget exceeded");
      glBufferData(GL_ARRAY_BUFFER,bytes,matrices->data(),GL_DYNAMIC_DRAW);geometry_bytes+=bytes-data.bytes;data.bytes=bytes;
    }else glBufferSubData(GL_ARRAY_BUFFER,0,bytes,matrices->data());
    data.matrices=matrices;++stats.instance_updates;return data.buffer;
  }
  const ShadowGeometry& shadow_geometry(const std::shared_ptr<const Mesh>& mesh,const SceneFrame& scene) {
    const auto key=mesh->animation_source ? mesh->animation_source.get() : mesh.get();
    auto mask=shadow_face_mask(*mesh,scene.materials);auto found=shadows.find(key);
    if(found!=shadows.end() && found->second.owner==mesh && found->second.mask==mask)return found->second;
    ShadowGeometry data;data.owner=mesh;data.mask=std::move(mask);data.mesh=prepare_shadow_mesh(*mesh,data.mask);
    const auto previous=found==shadows.end() ? 0 : found->second.bytes;
    data.bytes=data.mask.size()+data.mesh.vertices.size()*(sizeof(Vec3)+2*sizeof(std::array<float,4>))+data.mesh.planes.size()*sizeof(ShadowPlane)+data.mesh.edges.size()*sizeof(ShadowEdge)+(data.mesh.triangles.size()+data.mesh.triangle_planes.size())*sizeof(std::uint32_t);
    require(data.bytes<=512u*1024*1024-(geometry_bytes-previous),"scene shadow geometry budget exceeded");
    std::vector<std::array<float,4>> vertices;vertices.reserve(data.mesh.vertices.size()*2);
    for(const auto& v:data.mesh.vertices)for(float w:{1.f,0.f})vertices.push_back({v[0],v[1],v[2],w});
    if(found==shadows.end()){glGenBuffers(1,&data.buffer);++stats.shadow_uploads;}
    else {data.buffer=found->second.buffer;++stats.shadow_updates;}
    glBindBuffer(GL_ARRAY_BUFFER,data.buffer);
    const auto bytes=vertices.size()*sizeof(vertices[0]);
    if(found!=shadows.end() && vertices.size()==found->second.mesh.vertices.size()*2)glBufferSubData(GL_ARRAY_BUFFER,0,bytes,vertices.data());
    else glBufferData(GL_ARRAY_BUFFER,bytes,vertices.data(),mesh->animation_source ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    geometry_bytes=geometry_bytes-previous+data.bytes;
    if(found!=shadows.end()){found->second=std::move(data);return found->second;}
    return shadows.emplace(key,std::move(data)).first->second;
  }
  void shadow(const SceneInstance& instance,const SceneFrame& scene,const MaterialShader& shader,const SceneMatrix& projection,const SceneLight& light,bool zfail) {
    last_model=instance.world;
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
  void scene_materials(const SceneFrame& source,const FrameTarget& target) {
    std::optional<SceneFrame> animated;
    std::map<std::pair<const Mesh*,float>,std::shared_ptr<const Mesh>> poses;
    for(std::size_t i=0;i<source.instances.size();++i) {
      const auto& instance=source.instances[i];
      require(instance.mesh!=nullptr,"missing scene geometry");
      if(!mesh_has_animation(*instance.mesh))continue;
      require(std::isfinite(instance.time),"nonfinite mesh animation time");
      if(!animated)animated=source;
      const auto key=std::make_pair(instance.mesh.get(),instance.time);auto found=poses.find(key);
      if(found==poses.end())found=poses.emplace(key,pose_mesh(instance.mesh,instance.time)).first;
      animated->instances[i].mesh=found->second;
    }
    const auto& scene=animated ? *animated : source;
    const auto draws=scene_draw_jobs(scene,target.width,target.height);const auto projection=scene_raster_projection(scene.camera,target.width,target.height);
    if(albedo.color || std::any_of(scene.materials.begin(),scene.materials.end(),[](const auto& m){return std::any_of(m.second.passes.begin(),m.second.passes.end(),[](const auto& p){return bool(p.shader.material20);});}))albedo_surface(target);
    const auto restore_scissor=[&]{glScissor(0,surfaces.at(target.surface).height-target.height,target.width,target.height);};
    const auto insert=[&](unsigned usage) {
      restore_scissor();FrameQuad q;auto& w=q.shader.words;w[0]=0x30002|(usage==0 ? 0x3000 : 0x8000);w[9]=0x10;w[28]=0x00ffffff;w[23]=8;quad(q,target);
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
        const auto matrix=scene_combine(projection,instance.world);MeshDraw mesh{data.buffer,first,count,instance.world};
        if(pass.program==MaterialProgram::instances) {
          require(!pass.shader.material20,"Material 2.0 instance finalizer is not supported");
          mesh.instanced=true;
          mesh.instance_count=GLsizei(std::min(instance.particle_matrices ? instance.particle_matrices->size() : 0,data.instance_limits[job.batch]));
          if(mesh.instance_count)mesh.instance_buffer=instance_buffer(instance);
        }
        if(pass.shader.material20){material20(*pass.shader.material20,target,mesh,matrix,scene,job.light ? &draws.lights[*job.light].light : nullptr);return;}
        std::optional<LightDraw> setup;
        if(job.light) {
          const auto& l=draws.lights[*job.light].light;LightDraw draw;draw.directional=l.flags&4;
          draw.position=light_model_vector(instance.world,draw.directional ? l.direction : l.position,!draw.directional);draw.eye=light_model_vector(instance.world,{scene.camera.world[12],scene.camera.world[13],scene.camera.world[14]},true);
          const auto scale=.5f/l.range;for(unsigned c=0;c<3;++c)draw.attenuation[c]=draw.position[c]*(-scale)+.5f;draw.attenuation[3]=scale;
          draw.color=light_color(pass.shader,l);draw.specular=light_specular_parameters(as_float(pass.shader.words[32]));setup=draw;
        }
        std::optional<SceneFog> fog;if(pass.shader.words[0]&0x40)fog=scene_fog(scene.camera,instance.world);
        quad(resolve_material(pass.shader),target,nullptr,&matrix,&mesh,setup ? &*setup : nullptr,fog ? &*fog : nullptr,&scene.camera);
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
        restore_scissor();if(!draws.inserts[pass] && !draws.material20_inserts[pass])return;
        // Engine selects one insert per pass: Material20's priority 0x20 wins
        // over Material11's 0x10 when both occur in the same render pass.
        if(draws.material20_inserts[pass]) {if(usage==1)capture_albedo(target);}
        else if(usage==0 || usage==5)insert(usage);
        if(usage==4){glStencilMask(255);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);}
        if(usage==3)glDisable(GL_STENCIL_TEST);
      };
      std::optional<std::tuple<unsigned,unsigned,std::optional<std::size_t>>> current;
      for(const auto& job:draws.jobs) {
        const auto group=std::tuple(job.sort_key>>24,(job.sort_key>>16)&15,job.light);
        if(current!=group) {
          if(current)after(std::get<0>(*current),std::get<1>(*current));
          if((!current || std::get<0>(*current)!=std::get<0>(group)) && draws.capture_before_pass[std::get<0>(group)])capture_current(target);
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
FrameRenderer::Stats FrameRenderer::stats() const {auto stats=impl->stats;stats.bitmap_renders=impl->texture_options.rendered_bitmaps->stats().textures;stats.bitmap_bytes=impl->bitmap_bytes;return stats;}
void FrameRenderer::reset_targets() {
  auto& s=*impl;s.activate();s.last_model=scene_identity();glDisable(GL_SCISSOR_TEST);
  glClearStencil(0);glStencilMask(255);
  for(const auto& [id,surface]:s.surfaces) {
    static_cast<void>(id);glBindFramebuffer(GL_FRAMEBUFFER,surface.fbo);
    clear({3,0xff000000});glClear(GL_STENCIL_BUFFER_BIT);
  }
  if(s.shared_depth.fbo) {glBindFramebuffer(GL_FRAMEBUFFER,s.shared_depth.fbo);clear({2,0},true);}
  if(s.albedo.fbo) {glBindFramebuffer(GL_FRAMEBUFFER,s.albedo.fbo);clear({1,0xff000000});}
  for(const auto& image:s.glare_images)if(image.fbo){glBindFramebuffer(GL_FRAMEBUFFER,image.fbo);clear({1,0xff000000});}
  for(const auto& image:s.glare_scratch)if(image.fbo){glBindFramebuffer(GL_FRAMEBUFFER,image.fbo);clear({1,0xff000000});}
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
      if(const auto* q=std::get_if<FrameQuad>(&c.draw))s.quad(*q,c.target);
      else if(const auto* cl=std::get_if<FrameClear>(&c.draw))clear_target(*cl);
      else {const auto& scene=std::get<SceneFrame>(c.draw);s.capture_width=std::max(s.capture_width,c.target.width);s.capture_height=std::max(s.capture_height,c.target.height);clear_target({scene.viewport_flags&3,scene.background});if(diagnostic) {if(!scene.instances.empty())draw_scene_geometry(scene,c.target.width,c.target.height);s.effects(scene,c.target);}else s.scene_materials(scene,c.target);}
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
