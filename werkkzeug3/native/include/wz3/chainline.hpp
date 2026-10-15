// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
namespace wz3 {
class ChainLines {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  ChainLines();
  ~ChainLines();
  // State belongs to the effect's instance chain and advances even without a
  // material. A backward clock suppresses elapsed steps without clearing it.
  SceneRibbon update(std::size_t instance,std::span<const std::uint32_t>,const SceneMarkers&,const FrameClock&);
};
std::vector<SceneEffectVertex> chainline_vertices(const SceneRibbon&,const SceneMatrix& model_view);
} // namespace wz3
