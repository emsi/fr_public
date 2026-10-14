// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/mesh.hpp"
#include "wz3/frame.hpp"
#include <algorithm>
#include "wz3/texture.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  const bool materials=argc==5 && std::string_view(argv[4])=="--materials";
  if (argc != 4 && !materials) {
    std::cerr << "Usage: " << argv[0] << " PROJECT.k OPERATOR_NAME_OR_ID OUTPUT [--materials]\n";
    return 2;
  }
  try {
    const auto project = wz3::read_project(argv[1]);
    const auto graph = wz3::connect(project);
    const std::string_view selector(argv[2]);
    std::size_t id{};
    if (const auto found = graph.names.find(selector); found != graph.names.end()) id = found->second;
    else {
      const auto [end,error] = std::from_chars(selector.data(),selector.data()+selector.size(),id);
      if (error != std::errc{} || end != selector.data()+selector.size()) throw std::runtime_error("no mesh named " + std::string(selector));
    }
    const auto mesh = wz3::evaluate_mesh(project,graph,id);
#if WZ3_GPU_PREVIEW
    std::vector<std::uint8_t> pixels;
    if(materials) {
      // Isolated mesh inspection, with its own saved material links at beat zero.
      wz3::MaterialRuntime runtime(project,graph);wz3::SceneFrame scene;scene.viewport_flags=3;
      scene.instances.push_back({id,mesh});
      for(const auto& face:mesh->faces)if(face.material && !scene.materials.contains(*face.material))scene.materials.emplace(*face.material,runtime.snapshot(*face.material));
      const auto bounds=wz3::mesh_bounds(*mesh);float extent=0;
      for(unsigned c=0;c<3;++c) {scene.camera.world[12+c]=(bounds.min[c]+bounds.max[c])*.5f;extent=std::max(extent,bounds.max[c]-bounds.min[c]);}
      scene.camera.world[14]-=std::max(extent*1.5f,1.f);
      wz3::DemoFrame frame;frame.width=frame.height=768;frame.surfaces[0]={768,768};frame.commands.push_back({id,{0,768,768},std::move(scene)});
      wz3::FrameRenderer renderer(project,graph);pixels=renderer.render(frame);
    }else pixels=wz3::render_mesh(*mesh,768,768);
    wz3::write_ppm(argv[3],768,768,pixels);
#else
    if(materials)throw std::runtime_error("--materials requires wz3-mesh-render");
    wz3::write_obj(argv[3],*mesh);
#endif
    const auto bounds = wz3::mesh_bounds(*mesh);
    std::cout << "Operator " << id << ": " << mesh->vertices.size() << " vertices, " << mesh->faces.size() << " polygons\nBounds: [";
    for (unsigned c = 0; c < 3; ++c) std::cout << (c ? ", " : "") << bounds.min[c];
    std::cout << "] .. [";
    for (unsigned c = 0; c < 3; ++c) std::cout << (c ? ", " : "") << bounds.max[c];
    std::cout << "]\n" << (materials ? "Saved mesh materials at beat zero with the fallback inspection light; fitted camera. Scene animation and compositing are not evaluated.\n" : "Geometry preview: material shaders and scene animation are not evaluated.\n");
  } catch (const std::exception& error) { std::cerr << "mesh preview: " << error.what() << '\n'; return 1; }
}
