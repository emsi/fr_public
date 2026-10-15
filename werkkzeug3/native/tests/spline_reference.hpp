// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/spline3d.hpp"
namespace reference {
wz3::SplinePose shake(std::span<const std::uint32_t>,const wz3::SplinePose&,float amplitude,float global_time,
                      std::span<const std::array<std::int32_t,2>> events={},std::int32_t beat=0);
}
