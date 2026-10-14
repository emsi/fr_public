// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/fog.hpp"
#include <cmath>
namespace wz3 {
SceneFog scene_fog(const SceneCamera& camera,const SceneMatrix& model) {
  if(!std::isfinite(camera.fog_start) || !std::isfinite(camera.fog_end) || camera.fog_start==camera.fog_end)throw std::runtime_error("invalid scene fog interval");
  auto view=scene_identity();
  for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)view[c*4+r]=camera.world[r*4+c];
  for(unsigned r=0;r<3;++r)view[12+r]=-(view[r]*camera.world[12]+view[4+r]*camera.world[13]+view[8+r]*camera.world[14]);
  const auto m=scene_combine(view,model);SceneFog fog{{m[2],m[6],m[10],m[14]-camera.fog_end},{}};
  const auto scale=1.f/(camera.fog_start-camera.fog_end);
  for(auto& c:fog.plane){c*=scale;if(!std::isfinite(c))throw std::runtime_error("nonfinite scene fog transform");}
  for(unsigned c=0;c<3;++c)fog.color[c]=float((camera.fog_color>>(16-c*8))&255)/255.f;
  return fog;
}
std::string fog_fragment_shader(std::string source) {
  const auto main=source.find("void main()");if(main==std::string::npos)throw std::runtime_error("missing material shader entry point");
  source.replace(main,11,"void material_main()");
  source.insert(source.find('\n')+1,"in highp float material_fog;\nuniform highp vec3 fog_color;\n");
  return source+"\nvoid main(){material_main();color.rgb=mix(fog_color,color.rgb,clamp(material_fog,0.0,1.0));}\n";
}
} // namespace wz3
