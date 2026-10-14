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
  if(argc!=5 && argc!=6) {std::cerr<<"Usage: "<<argv[0]<<" PROJECT.k NAME_OR_ID BEATS OUTPUT [EVENT_INDEX | --event]\n";return 2;}
  try {
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);wz3::ScenePlayer player(project,graph);
    const auto found=graph.names.find(argv[2]);const auto id=found==graph.names.end() ? number<std::size_t>(argv[2]) : found->second;
    const auto time=number<double>(argv[3])*65536;
    if(!std::isfinite(time) || time<-2147483648.0 || time>2147483647.0)throw std::runtime_error("beat outside 16.16 range");
    const auto beat=std::int32_t(time);auto env=wz3::frame_environment(beat);auto world=wz3::scene_identity();
    wz3::EventTimeline timeline(project,graph);timeline.begin_frame(beat);
    const wz3::SplineLibrary splines(project);std::unique_ptr<wz3::EventAnimationScope> event;
    const bool dispatch=argc==6 && std::string_view(argv[5])=="--event";
    if(argc==6 && !dispatch) {
      const auto& saved=project.events.at(number<std::size_t>(argv[5]));
      if(!wz3::event_active(saved,beat))throw std::runtime_error("selected event is inactive at this beat");
      event=std::make_unique<wz3::EventAnimationScope>(saved,beat,splines,env);world=wz3::scene_srt(saved.transform);
    }
    wz3::SceneFrame frame;
    if(dispatch) {
      auto frames=player.event_viewports(id,timeline,4.0f/3);
      if(frames.size()!=1)throw std::runtime_error("single-image output needs exactly one active viewport; found "+std::to_string(frames.size()));
      frame=std::move(frames.front());
    } else frame=player.viewport(id,env,4.0f/3,world,event ? &timeline : nullptr);
#if WZ3_GPU_PREVIEW
    const auto pixels=wz3::render_scene(frame,1024,768);wz3::write_ppm(argv[4],1024,768,pixels);
#else
    wz3::write_scene(argv[4],frame);
#endif
    std::cout<<"Viewport "<<frame.viewport_id<<": "<<frame.instances.size()<<" mesh instances, "<<frame.lights.size()<<" lights, "<<frame.visited_operators<<" scene visits\n"
      <<"Saved camera and scene animation are evaluated. Material shaders, effects and postprocessing are not rendered.\n";
  }catch(const std::exception& error) {std::cerr<<"scene: "<<error.what()<<'\n';return 1;}
}
