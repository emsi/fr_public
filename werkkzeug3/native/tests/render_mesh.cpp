// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/mesh.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

int main() {
  try {
    wz3::Mesh mesh;
    for (unsigned layer = 0; layer < 2; ++layer) {
      const float z = layer ? -0.1f : 0.1f, u = layer ? 0.125f : 0;
      for (const auto& xy : {std::array<float,2>{-1,-1},{1,-1},{1,1},{-1,1}})
        mesh.vertices.push_back({{xy[0],xy[1],z},{u,0,0},0});
      const unsigned first = layer*4;
      mesh.faces.push_back({{first,first+1,first+2,first+3},4,0,{},0});
    }
    const auto a = wz3::render_mesh(mesh,256,256);
    std::reverse(mesh.faces.begin(),mesh.faces.end());
    const auto b = wz3::render_mesh(mesh,256,256);
    if (a != b) throw std::runtime_error("depth occlusion depends on polygon submission order");
    std::cout << "GPU depth occlusion is independent of polygon order\n";
  } catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
