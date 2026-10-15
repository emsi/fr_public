// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "minmesh_kernel.hpp"
namespace wz3::legacy::minimal {
struct Font3DSegment {bool quadratic{};GenMinVector control,end;};
struct Font3DContour {GenMinVector start;std::vector<Font3DSegment> segments;};
using Font3DGlyph=std::vector<Font3DContour>;
std::vector<Font3DGlyph> font3d_outlines(const std::filesystem::path&,std::string_view,float height);
std::shared_ptr<GenMinMesh> font3d_paths(std::span<const Font3DGlyph>,float height,float extrude,float max_error);
std::shared_ptr<GenMinMesh> font3d_mesh(const std::filesystem::path&,std::string_view,float height,float extrude,float max_error);
}
