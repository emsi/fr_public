// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "gles.hpp"
#include <iostream>
namespace {
void pixel(unsigned x,unsigned y,std::array<std::uint8_t,4> expected) {
  std::array<std::uint8_t,4> got{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,got.data());
  check(got==expected,"window pixel orientation, scaling or black bars incorrect");
}
}
int main() {
  try {
    wz3::gles::Context context(64,64);
    FrameFixture x;const auto top=x.layer(x.material(0xffff0000),3,{0,0,1,.5f});
    const auto bottom=x.layer(x.material(0xff0000ff),3,{0,.5f,1,1});const auto root=x.add(0x0d,{}, {top,bottom});
    wz3::FramePlayer player(x.p,x.g);
    {
      wz3::FrameRenderer renderer(x.p,x.g,{},wz3::FrameRenderer::Context::Current);
      renderer.draw(player.frame(root,0,32,16));const auto saved=renderer.readback();
      check(saved[0]==255 && saved[2]==0,"readback must remain top-down");
      renderer.present(64,64);pixel(4,2,{0,0,0,255});pixel(4,62,{0,0,0,255});
      pixel(4,20,{0,0,255,255});pixel(4,44,{255,0,0,255});
      renderer.present(64,16);pixel(2,8,{0,0,0,255});pixel(62,8,{0,0,0,255});
      pixel(20,2,{0,0,255,255});pixel(20,14,{255,0,0,255});
      check(renderer.readback()==saved,"presentation modified the retained screen");
      renderer.draw(player.frame(root,65536,32,16));renderer.present(64,64);
      pixel(4,20,{0,0,255,255});pixel(4,44,{255,0,0,255});
    }
    check(eglGetCurrentContext()==context.context,"renderer destroyed borrowed context");
    check(glGetError()==GL_NO_ERROR,"presentation GL error");
    std::cout<<"Borrowed context, GPU presentation, orientation and letterboxing passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
