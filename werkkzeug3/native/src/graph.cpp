// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// References: WerkPage::Connect1/2, WerkDoc::Connect and FindRoot.
#include "wz3/graph.hpp"
#include <algorithm>
#include <stdexcept>

namespace wz3 {
Graph connect(const Project& project) {
  Graph graph;
  graph.nodes.resize(project.operators.size());
  std::vector<std::vector<std::size_t>> pages(project.pages.size());
  for (std::size_t i = 0; i < project.operators.size(); ++i) {
    const auto& op = project.operators[i];
    auto& node = graph.nodes[i];
    node.active = !op.hidden && op.scratch == (project.pages.at(op.page).fields[6] != 0);
    if (!node.active) continue;
    pages[op.page].push_back(i);
    if (!op.name.empty()) {
      if (!graph.names.emplace(op.name, i).second)
        graph.warnings.push_back("duplicate active name: " + op.name);
      if (op.bypass) graph.warnings.push_back("bypassed operator has a name: " + op.name);
    }
  }
  if(project.runtime_graph) {
    if(project.runtime_inputs.size()!=graph.nodes.size())throw std::runtime_error("runtime graph size mismatch");
    for(std::size_t i=0;i<graph.nodes.size();++i) {
      for(auto input:project.runtime_inputs[i])if(input>=i)throw std::runtime_error("runtime input is not topologically ordered");
      graph.nodes[i].inputs=project.runtime_inputs[i];
    }
    for(unsigned i=0;i<16;++i)if(project.runtime_roots[i]<graph.nodes.size())
      graph.names.emplace(i==0 ? "root" : "root_"+std::to_string(i),project.runtime_roots[i]);
  } else for (auto& page : pages) {
    std::stable_sort(page.begin(), page.end(), [&](auto a, auto b) {
      return project.operators[a].y < project.operators[b].y;
    });
    for (std::size_t i = 0; i < page.size(); ++i) {
      const auto id = page[i];
      const auto& op = project.operators[id];
      auto& inputs = graph.nodes[id].inputs;
      for (std::size_t j = i; j > 0;) {
        auto input = page[--j];
        const auto& candidate = project.operators[input];
        if (std::int64_t(candidate.y) < std::int64_t(op.y) - 1) break;
        if (std::int64_t(candidate.y) != std::int64_t(op.y) - 1 ||
            std::int64_t(candidate.x) + candidate.width <= op.x ||
            candidate.x >= std::int64_t(op.x) + op.width) continue;
        bool valid = true;
        while (project.operators[input].bypass) {
          if (!project.operators[input].name.empty() || graph.nodes[input].inputs.empty()) {
            valid = false;
            break;
          }
          input = graph.nodes[input].inputs.front();
        }
        if (valid) inputs.push_back(input);
      }
      std::stable_sort(inputs.begin(), inputs.end(), [&](auto a, auto b) {
        return project.operators[a].x < project.operators[b].x;
      });
    }
  }
  for (std::size_t i = 0; i < project.operators.size(); ++i) {
    auto& node = graph.nodes[i];
    if (!node.active) continue;
    for (const auto& name : project.operators[i].links) {
      const auto found = graph.names.find(name);
      if (found == graph.names.end()) {
        node.links.push_back(std::nullopt);
        if (!name.empty()) graph.warnings.push_back("operator " + std::to_string(i) + ": unresolved link " + name);
      } else node.links.push_back(found->second);
    }
  }
  for (unsigned i = 0; i < 16; ++i) {
    auto root = graph.names.find("root_" + std::to_string(i));
    if (i == 0) {
      const auto alias = graph.names.find("root");
      if (alias != graph.names.end() && root != graph.names.end()) {
        graph.warnings.push_back("ambiguous root and root_0");
        continue;
      }
      if (root == graph.names.end()) root = alias;
    }
    if (root != graph.names.end()) graph.entry_points.push_back(root->second);
  }
  for (const auto& event : project.events) {
    if (event.fields[10]) continue; // disabled event
    // Connect() resolves the event name afresh, not its saved operator index.
    const auto found = graph.names.find(event.name);
    if (found != graph.names.end()) graph.entry_points.push_back(found->second);
    else if (!event.name.empty()) graph.warnings.push_back("unresolved event: " + event.name);
  }
  std::sort(graph.entry_points.begin(), graph.entry_points.end());
  graph.entry_points.erase(std::unique(graph.entry_points.begin(), graph.entry_points.end()), graph.entry_points.end());
  auto pending = graph.entry_points;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    auto& node = graph.nodes[id];
    if (node.reachable) continue; // named links can form cycles
    node.reachable = true;
    pending.insert(pending.end(), node.inputs.begin(), node.inputs.end());
    for (const auto link : node.links) if (link) pending.push_back(*link);
  }
  return graph;
}
} // namespace wz3
