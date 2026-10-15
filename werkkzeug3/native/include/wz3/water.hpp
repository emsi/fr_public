// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace wz3 {
namespace legacy {struct BlobSpline;}
struct WaterEffect {
  std::array<std::uint32_t,6> parameters{};
  std::shared_ptr<const legacy::BlobSpline> spline;
  float time{};
};
struct WaterGeometry {
  unsigned width{};
  // Archived FVF_TSPACE: position, normal, two packed colors, UV, S and T.
  // Colors occupy float slots 6/7 as bit patterns, not numeric float values.
  std::vector<float> vertices;
  std::vector<std::uint16_t> indices;
};
WaterGeometry water_geometry(const WaterEffect&);
}
