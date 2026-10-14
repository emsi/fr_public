// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include "wz3/fog.hpp"
#include <iostream>
int main(){try{
  wz3::SceneCamera camera;camera.fog_start=2;camera.fog_end=4;camera.fog_color=0x40123456;
  auto fog=wz3::scene_fog(camera,wz3::scene_identity());check(fog.plane==std::array<float,4>{0,0,-.5f,2},"default fog plane");near(fog.color[0],18.f/255,"fog red channel");near(fog.color[2],86.f/255,"fog blue channel");
  for(unsigned i=0;i<30;++i) {
    const std::array<std::uint32_t,9> cs={f(1),f(1),f(1),f(i*.01f),f(i*.03f),f(i*-.02f),f(i*.2f),f(i*-.1f),f(i*.3f)};
    const std::array<std::uint32_t,9> ms={f(2),f(.5f),f(3),f(i*-.02f),f(i*.015f),f(i*.025f),f(2),f(-3),f(4)};
    camera.world=wz3::scene_srt(cs);const auto model=wz3::scene_srt(ms);fog=wz3::scene_fog(camera,model);
    for(const wz3::Vec3 p:std::array<wz3::Vec3,3>{{{0,0,0},{1,2,3},{-1,-2,4}}}) {
      const auto world=wz3::scene_point(model,p);double depth=0,actual=fog.plane[3];
      for(unsigned c=0;c<3;++c){depth+=(double(world[c])-camera.world[12+c])*camera.world[8+c];actual+=double(p[c])*fog.plane[c];}
      check(std::abs(actual-(camera.fog_end-depth)/(camera.fog_end-camera.fog_start))<.00001,"model/camera fog plane differs from view depth");
    }
  }
  camera.world=wz3::scene_identity();std::swap(camera.fog_start,camera.fog_end);fog=wz3::scene_fog(camera,wz3::scene_identity());near(fog.plane[2],.5f,"reversed fog interval must retain source behavior");
  camera.fog_start=camera.fog_end;bool rejected=false;try{wz3::scene_fog(camera,wz3::scene_identity());}catch(const std::runtime_error&){rejected=true;}check(rejected,"undefined zero-width fog interval accepted");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
