// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/project.hpp"
#include <map>
#include <optional>

namespace wz3 {
struct Node {
  bool active{}, reachable{};
  std::vector<std::size_t> inputs;
  std::vector<std::optional<std::size_t>> links;
};
struct Graph {
  std::vector<Node> nodes;
  std::map<std::string, std::size_t, std::less<>> names;
  std::vector<std::size_t> entry_points;
  std::vector<std::string> warnings;
};
// Reconstruct spatial inputs, named links and root/event reachability.
// This is not the original editor's full operator type/error checker.
Graph connect(const Project& project);
} // namespace wz3
