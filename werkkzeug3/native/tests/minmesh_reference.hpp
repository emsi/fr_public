// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "minmesh_kernel.hpp"
#include <span>
std::unique_ptr<wz3::legacy::minimal::GenMinMesh> minmesh_conversion_reference(const wz3::legacy::GenMesh&);
void check_minmesh_shadow_reference(const wz3::Mesh&,std::span<const std::uint8_t>);
