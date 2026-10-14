// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/scene.hpp"
#include "wz3/texture.hpp"
#include <charconv>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
template<class T> T number(std::string_view text) {
  T value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if(error!=std::errc{} || end!=text.data()+text.size())throw std::runtime_error("invalid numeric argument");
  return value;
}
}
int main(int argc,char** argv) {
  if(argc!=5 && argc!=6) {std::cerr<<"Usage: "<<argv[0]<<" PROJECT.k VIEWPORT_NAME_OR_ID BEATS OUTPUT [EVENT_INDEX]\n";return 2;}
  try {
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);wz3::ScenePlayer player(project,graph);
    const auto found=graph.names.find(argv[2]);const auto id=found==graph.names.end() ? number<std::size_t>(argv[2]) : found->second;
    const auto time=number<double>(argv[3])*65536;
    if(!std::isfinite(time) || time<-2147483648.0 || time>2147483647.0)throw std::runtime_error("beat outside 16.16 range");
    const auto beat=std::int32_t(time);auto env=wz3::frame_environment(beat);auto world=wz3::scene_identity();
    const wz3::SplineLibrary splines(project);std::unique_ptr<wz3::EventAnimationScope> event;
    if(argc==6) {
      const auto& saved=project.events.at(number<std::size_t>(argv[5]));
      if(!wz3::event_active(saved,beat))throw std::runtime_error("selected event is inactive at this beat");
      event=std::make_unique<wz3::EventAnimationScope>(saved,beat,splines,env);world=wz3::scene_srt(saved.transform);
    }
    const auto frame=player.viewport(id,env,4.0f/3,world);
#if WZ3_GPU_PREVIEW
    const auto pixels=wz3::render_scene(frame,1024,768);wz3::write_ppm(argv[4],1024,768,pixels);
#else
    wz3::write_scene(argv[4],frame);
#endif
    std::cout<<"Viewport "<<frame.viewport_id<<": "<<frame.instances.size()<<" mesh instances, "<<frame.lights.size()<<" lights, "<<frame.visited_operators<<" scene visits\n"
      <<"Saved camera and scene animation are evaluated. Material shaders, effects and postprocessing are not rendered.\n";
  }catch(const std::exception& error) {std::cerr<<"scene: "<<error.what()<<'\n';return 1;}
}
