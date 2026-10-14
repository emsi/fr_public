// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/scene.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::shared_ptr<const wz3::Mesh> plane(float u) {
  auto mesh=std::make_shared<wz3::Mesh>();
  for(const auto& xy:{std::array<float,2>{-.8f,-.8f},{.8f,-.8f},{.8f,.8f},{-.8f,.8f}})mesh->vertices.push_back({{xy[0],xy[1],0},{u,0,0},0});
  mesh->faces.push_back({{0,1,2,3},4,0,{},0});return mesh;
}
}
int main() {
  try {
    wz3::SceneFrame frame;frame.camera.far_clip=.5f;
    wz3::SceneInstance front;front.mesh=plane(0);front.world[14]=1;
    wz3::SceneInstance back;back.mesh=plane(.125f);back.world[14]=2;
    frame.instances={front,back};const auto a=wz3::render_scene(frame,256,256);
    check(a[(128*256+128)*4]!=0,"positive-Z geometry beyond stored far distance must remain visible");
    std::reverse(frame.instances.begin(),frame.instances.end());check(a==wz3::render_scene(frame,256,256),"perspective depth depends on instance submission order");
    frame.instances={front};check(a==wz3::render_scene(frame,256,256),"nearer instance must occlude the farther checker");
    frame.camera.world[12]=.5f;check(a!=wz3::render_scene(frame,256,256),"camera translation did not change the image");
    frame.camera.world=wz3::scene_identity();frame.instances[0].world[14]=.0625f;
    const auto clipped=wz3::render_scene(frame,256,256);
    for(std::size_t i=0;i<clipped.size();i+=4)check(clipped[i]==0 && clipped[i+1]==0 && clipped[i+2]==0,"geometry before the near plane was not clipped");
    std::cout<<"native perspective, positive-Z orientation, near clipping, depth and camera motion passed\n";
  }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
