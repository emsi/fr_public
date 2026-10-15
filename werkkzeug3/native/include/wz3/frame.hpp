// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/scene.hpp"
#include <variant>

namespace wz3 {
// Install a cached Bitmap_Render backend. It evaluates fresh time-zero scenes
// in isolated GLES contexts and restores any caller context on return/failure.
TextureOptions rendered_texture_options(const Project&,const Graph&,TextureOptions = {});

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
  bool released_layer{}; // Normalized Layer2D uses D3D9 integer pixel centers.
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
  FramePlayer(const Project&,const Graph&,const TextureOptions& = {},std::shared_ptr<SceneAssets> = {});
  ~FramePlayer();
  FramePlayer(const FramePlayer&)=delete;
  FramePlayer& operator=(const FramePlayer&)=delete;
  // Omitted milliseconds are derived from the saved BPM for offline sampling.
  DemoFrame frame(std::size_t root,std::int32_t beat,unsigned width,unsigned height,
                  std::optional<std::int32_t> milliseconds={});
  // Bitmap_Render initialization: use a fresh player; time zero, aspect one,
  // no project events and no final copy from the IPP pool to the master target.
  DemoFrame bitmap_frame(std::size_t ipp,unsigned width,unsigned height);
};
void write_frame(std::ostream&,const DemoFrame&);
// Keeps render targets, textures and saved-material mesh uploads across frames.
// Unsupported scene shading fails explicitly. diagnostic_geometry=true selects
// checker mesh shading; effects, IPP and Layer2D retain their saved materials.
class FrameRenderer {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  enum class Context { Headless, Current };
  struct Stats {
    std::size_t surface_allocations{},bitmap_uploads{},mesh_uploads{},shadow_uploads{},program_compilations{},lighting_uploads{};
    std::size_t mesh_updates{},shadow_updates{},instance_uploads{},instance_updates{},bitmap_renders{};
    bool operator==(const Stats&) const = default;
  };
  // Current borrows the caller's GLES3 context. Keep it current for every call,
  // including destruction, and keep it alive longer than the renderer.
  FrameRenderer(const Project&,const Graph&,const TextureOptions& = {},Context=Context::Headless);
  ~FrameRenderer();
  std::vector<std::uint8_t> render(const DemoFrame&,bool diagnostic_geometry=false);
  void draw(const DemoFrame&,bool diagnostic_geometry=false);
  std::vector<std::uint8_t> readback(); // last screen, top-down RGBA8
  // Blit the last screen to framebuffer 0, preserving aspect with black bars.
  // The caller owns the window/swap; this does not read pixels back to the CPU.
  void present(unsigned drawable_width,unsigned drawable_height);
  // Discard all image/depth/stencil history after a loading pass, preserving
  // static uploads and shaders. Waits for pending GPU work before returning.
  void reset_targets();
  Stats stats() const;
};
} // namespace wz3
