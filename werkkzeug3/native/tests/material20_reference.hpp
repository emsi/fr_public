// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/material.hpp"
namespace reference {
// -1 means absent, -2 another object class, nonnegative values are bitmap IDs.
wz3::Material material20(std::span<const std::uint32_t>,const std::array<int,7>& links,
                        const std::array<int,3>& inputs,std::span<const std::uint32_t> update={});
}
