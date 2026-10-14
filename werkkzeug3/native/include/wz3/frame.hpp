// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
#include <variant>

namespace wz3 {
using FrameRect=std::array<float,4>; // left, top, right, bottom
struct FrameSurface {unsigned width{},height{};};
struct FrameTarget {std::size_t surface{};unsigned width{},height{};};
struct FrameTexture {bool surface{};std::size_t id{};};
struct FrameClear {std::uint32_t flags{},color{};};
struct FrameQuad {
  MaterialShader shader;
  std::array<std::optional<FrameTexture>,4> textures;
  FrameRect rectangle{0,0,1,1};
  std::array<FrameRect,4> uv{{{0,0,1,1},{0,0,1,1},{0,0,1,1},{0,0,1,1}}};
  float depth=1.0f/4096;
};
struct FrameCommand {
  std::size_t operator_id{};
  FrameTarget target;
  std::variant<FrameClear,FrameQuad,SceneFrame> draw;
};
struct DemoFrame {
  std::int32_t beat{},milliseconds{};
  unsigned width{},height{};
  std::size_t visited_operators{},cache_hits{},screen_fallbacks{};
  // 0 is the screen, 1..12 the legacy pool, 13+operator_id a bitmap target.
  std::map<std::size_t,FrameSurface> surfaces;
  std::vector<FrameCommand> commands;
};
// Executes the saved root, including ancestor animation scopes, static events,
// shared materials and ordered IPP operations. GPU resources live separately.
class FramePlayer {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  FramePlayer(const Project&,const Graph&,const TextureOptions& = {});
  ~FramePlayer();
  FramePlayer(const FramePlayer&)=delete;
  FramePlayer& operator=(const FramePlayer&)=delete;
  // Omitted milliseconds are derived from the saved BPM for offline sampling.
  DemoFrame frame(std::size_t root,std::int32_t beat,unsigned width,unsigned height,
                  std::optional<std::int32_t> milliseconds={});
};
void write_frame(std::ostream&,const DemoFrame&);
// Keeps render targets and generated texture uploads alive across frames.
// Until the scene material engine is ported, scene draws require an explicit
// diagnostic_geometry=true. IPP and Layer2D still use their saved materials.
class FrameRenderer {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  FrameRenderer(const Project&,const Graph&,const TextureOptions& = {});
  ~FrameRenderer();
  std::vector<std::uint8_t> render(const DemoFrame&,bool diagnostic_geometry=false);
};
} // namespace wz3
