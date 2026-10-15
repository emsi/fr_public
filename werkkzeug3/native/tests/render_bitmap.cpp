// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "gles.hpp"
#include <algorithm>
#include <iostream>
namespace {
using namespace wz3;
template<class F> void rejects(F action) {
  bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}
  check(caught,"invalid Render dependency accepted");
}
std::size_t textured(FrameFixture& x,std::size_t bitmap) {
  const auto mat=x.material(0xffffffff);auto& p=x.p.operators[mat].parameters;
  p[9]=0;p[11]=0x10;p[23]=4;p[4]=0x104;x.g.nodes[mat].links={bitmap};return mat;
}
std::size_t pattern(FrameFixture& x) {
  const auto ma=x.material(0x4080c020),mb=x.material(0xa0104090);
  x.p.operators[ma].parameters[23]=x.p.operators[mb].parameters[23]=8;
  const auto a=x.layer(ma,4,{0,0,.5f,.5f});
  const auto b=x.layer(mb,4,{.5f,.5f,1,1});
  return x.add(0xea,{}, {a,b});
}
void pattern_check(const Texture& t) {
  for(unsigned y=0;y<t.height;++y)for(unsigned x=0;x<t.width;++x) {
    const auto color=x<t.width/2 && y<t.height/2 ? 0x4080c020 : x>=t.width/2 && y>=t.height/2 ? 0xa0104090 : 0xffff8080;
    check(t.pixels[y*t.width+x]==color64(color),"baked image color/alpha/row orientation or clear differs");
  }
}
void standalone() {
  FrameFixture x;const auto ipp=pattern(x),id=x.add(0x3f,{5,4},{ipp});
  const auto options=rendered_texture_options(x.p,x.g);
  check(eglGetCurrentContext()==EGL_NO_CONTEXT,"standalone test has a caller context");
  auto baked=evaluate_texture(x.p,x.g,id,options);pattern_check(*baked);
  check(eglGetCurrentContext()==EGL_NO_CONTEXT,"standalone bake retained context");
  check(evaluate_texture(x.p,x.g,id,options)==baked,"bake cache did not retain texture identity");
  check(options.rendered_bitmaps->stats().textures==1 && options.rendered_bitmaps->stats().pixel_bytes==32*16*sizeof(Pixel),"bake cache accounting");
}
void contexts() {
  FrameFixture x;const auto ipp=pattern(x),id=x.add(0x3f,{5,4},{ipp});
  const auto bad=x.add(0xf0),failed=x.add(0x3f,{5,4},{bad});
  const auto options=rendered_texture_options(x.p,x.g);
  gles::Context context(16,16);
  const auto parent=eglGetCurrentContext();const auto display=eglGetCurrentDisplay();const auto surface=eglGetCurrentSurface(EGL_DRAW);
  GLuint texture,buffer,vao;glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,texture);
  glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  glViewport(1,2,9,6);glEnable(GL_BLEND);glClearColor(.25f,.5f,.75f,1);glClear(GL_COLOR_BUFFER_BIT);
  std::array<std::uint8_t,4> before;glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,before.data());
  auto unchanged=[&]{
    check(eglGetCurrentContext()==parent && eglGetCurrentDisplay()==display && eglGetCurrentSurface(EGL_DRAW)==surface && eglGetCurrentSurface(EGL_READ)==surface,"bake did not restore caller EGL context/surfaces");
    GLint value;glGetIntegerv(GL_ACTIVE_TEXTURE,&value);check(value==GL_TEXTURE3,"active texture changed");
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&value);check(GLuint(value)==texture,"texture binding changed");
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&value);check(GLuint(value)==buffer,"buffer binding changed");
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&value);check(GLuint(value)==vao,"VAO binding changed");
    std::array<GLint,4> viewport;glGetIntegerv(GL_VIEWPORT,viewport.data());check(viewport==std::array<GLint,4>{1,2,9,6},"viewport changed");
    check(glIsEnabled(GL_BLEND),"blend state changed");std::array<std::uint8_t,4> after;glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,after.data());check(after==before,"bake modified caller framebuffer");check(glGetError()==GL_NO_ERROR,"caller GL error after bake");
  };
  pattern_check(*evaluate_texture(x.p,x.g,id,options));unchanged();
  rejects([&]{evaluate_texture(x.p,x.g,failed,options);});unchanged();
  // Retrying a failed operator must not retain a pending dependency/reservation.
  x.g.nodes[failed].inputs={ipp};pattern_check(*evaluate_texture(x.p,x.g,failed,options));unchanged();
  check(options.rendered_bitmaps->stats().textures==2,"failed bake entered cache");
  glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&buffer);glDeleteTextures(1,&texture);
}
void pool_and_events() {
  FrameFixture x;const auto mat=x.material(0xff224466),pool=x.layer(mat,0);
  const auto pool_bake=x.add(0x3f,{4,3},{pool});
  const auto event=x.event(x.layer(mat,4),"scheduled",0,65536),event_bake=x.add(0x3f,{4,3},{event});
  const auto options=rendered_texture_options(x.p,x.g);
  for(auto id:{pool_bake,event_bake}) {
    const auto texture=evaluate_texture(x.p,x.g,id,options);
    check(std::all_of(texture->pixels.begin(),texture->pixels.end(),[](auto p){return p==color64(0xffff8080);}),"bake copied the final IPP pool or dispatched project events");
  }
}
void nested_and_materials() {
  FrameFixture x;const auto red=x.layer(x.material(0xffff0000),4),inner=x.add(0x3f,{4,4},{red});
  const auto outer=x.add(0x3f,{4,4},{x.layer(textured(x,inner),4)});
  const auto flat=x.add(0x21,{4,4,0xffff0000}),first=x.add(0x21,{4,4,0xff204080});
  auto two_textures=[&](std::size_t second){const auto mat=textured(x,first);x.g.nodes[mat].links.push_back(second);auto& p=x.p.operators[mat].parameters;p[12]=0x20;p[5]=0x104;return x.layer(mat,4);};
  const auto actual=two_textures(outer),control=two_textures(flat);
  const auto options=rendered_texture_options(x.p,x.g);gles::Context context(16,16);
  FramePlayer player(x.p,x.g,options);FrameRenderer renderer(x.p,x.g,options,FrameRenderer::Context::Current);
  const auto expected=renderer.render(player.frame(control,0,32,16,0));
  const auto result=renderer.render(player.frame(actual,4*65536,32,16,2000));
  check(result==expected,"nested bake corrupted earlier material texture bindings");
  check(options.rendered_bitmaps->stats().textures==2 && renderer.stats().bitmap_renders==2,"nested bake count");
  const auto stats=renderer.stats();renderer.reset_targets();
  check(renderer.render(player.frame(actual,8*65536,32,16,4000))==result,"bake changed with playback time/reset");
  check(renderer.stats()==stats,"warm playback allocated or rebaked static texture");
  // A second player shares the precomputed images, but has independent state.
  FramePlayer fresh(x.p,x.g,options);check(renderer.render(fresh.frame(actual,0,32,16,0))==expected,"fresh playback lost baked pixels");
}
void meshes() {
  FrameFixture x;const auto flat=x.add(0x21,{4,4,0xffff0000}),baked=x.add(0x3f,{4,4},{x.layer(x.material(0xffff0000),4)});
  const auto cube=x.add(0x81,{1,1,1,1,f(1),f(1),f(1),0,0,0,0,0,0});
  std::vector<std::size_t> controls,actuals;
  for(auto bitmap:{flat,baked}) {
    auto& ids=bitmap==flat ? controls : actuals;
    const auto displace=x.add(0x8f,{0,f(1),f(.2f),f(.3f)},{cube,bitmap});ids.push_back(displace);
    const auto minimal=x.add(0xb5,{}, {displace});ids.push_back(minimal);
    std::vector<std::uint32_t> p(21);p[3]=f(.1f);p[4]=f(2);p[11]=f(2);p[12]=f(1);p[16]=f(10);
    ids.push_back(x.add(0x134,p,{minimal,bitmap}));
  }
  const auto options=rendered_texture_options(x.p,x.g);SceneAssets assets(x.p,x.g,options);
  for(unsigned i=0;i<controls.size();++i) {
    const auto a=pose_mesh(assets.mesh(actuals[i]),1),b=pose_mesh(assets.mesh(controls[i]),1);
    check(a->vertices.size()==b->vertices.size() && !a->vertices.empty(),"rendered bitmap mesh vertex count");
    for(unsigned j=0;j<a->vertices.size();++j)check(a->vertices[j].position==b->vertices[j].position,"rendered bitmap Displace/conversion/Explode differs from flat reference");
  }
  check(options.rendered_bitmaps->stats().textures==1,"mesh dependencies rebaked texture");
}
void cycles() {
  FrameFixture x;const auto id=x.add(0x3f,{4,4}),layer=x.layer(textured(x,id),4);x.g.nodes[id].inputs={layer};
  const auto valid=x.layer(x.material(0xffff0000),4);
  const auto options=rendered_texture_options(x.p,x.g);gles::Context context(16,16);const auto parent=eglGetCurrentContext();
  rejects([&]{evaluate_texture(x.p,x.g,id,options);});
  check(eglGetCurrentContext()==parent && options.rendered_bitmaps->stats().textures==0,"cycle did not restore context/cache");
  x.g.nodes[id].inputs={valid};check(evaluate_texture(x.p,x.g,id,options)->pixels[0]==color64(0xffff0000),"cycle poisoned later bakes");
}
void debris(const char* path,const char* font) {
  const auto p=read_project(path);const auto g=connect(p);auto options=rendered_texture_options(p,g,{font});
  unsigned count=0;
  for(std::size_t id=0;id<p.operators.size();++id)if(g.nodes[id].reachable && p.operators[id].command==0x3f) {
    std::cout<<"Checking saved Debris bitmap "<<id<<std::endl;
    const auto baked=evaluate_texture(p,g,id,options);check(baked->width==512 && baked->height==512,"Debris rendered bitmap size");
    check(std::any_of(baked->pixels.begin(),baked->pixels.end(),[](auto pixel){return pixel!=color64(0xffff8080);}),"Debris baked bitmap remained clear");
    check(evaluate_texture(p,g,id,options)==baked,"saved Debris bitmap not cached");
    // All saved renders are square. Ordinary time-zero IPP execution provides
    // an independent path through frame setup; no demo-level final copy.
    FramePlayer reference(p,g,options);auto frame=reference.frame(g.nodes[id].inputs.at(0),0,512,512,0);
    frame.commands.insert(frame.commands.begin(),{id,{0,512,512},FrameClear{3,0xffff8080}});
    FrameRenderer renderer(p,g,options);check(bitmap_readback(512,512,renderer.render(frame)).pixels==baked->pixels,"saved bake differs from standalone IPP frame");
    ++count;
  }
  // This downstream HSCB/store texture is the material dependency that first
  // stopped continuous playback at 110.207 seconds.
  const auto filtered=evaluate_texture(p,g,10385,options);check(filtered->pixels.size()==512*512,"Debris bake/filter dependency");
  check(count==3 && options.rendered_bitmaps->stats().textures==3,"Debris bitmap Render coverage");
  std::cout<<count<<" saved Debris rendered bitmaps and the blocking material texture passed\n";
}
}
int main(int argc,char** argv){try{standalone();contexts();pool_and_events();nested_and_materials();meshes();cycles();if(argc==3)debris(argv[1],argv[2]);std::cout<<"GPU bitmap baking: colors/alpha/orientation, nested contexts, failures, cache reuse, events, IPP pool and mesh consumers passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
