// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/graph.hpp"
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
  try {
    wz3::Project project;
    project.pages.resize(1);
    auto add = [&](int x, int y, int width, std::string name = "") {
      wz3::Operator op;
      op.x = x; op.y = y; op.width = width; op.name = std::move(name);
      project.operators.push_back(std::move(op));
    };
    add(4, 0, 2); // 0: right input, stored before left input
    add(0, 0, 2); // 1: left input
    add(0, 1, 6); // 2: bypass -> first (left) input
    project.operators[2].bypass = true;
    add(0, 2, 6, "source"); // 3
    add(10, 10, 2, "root"); // 4: named link to source
    project.operators[4].links = {"source", ""};
    add(0, 1, 6); // 5: hidden, must not be an input
    project.operators[5].hidden = true;
    add(20, 20, 2, "event"); // 6: event name overrides stale saved index
    wz3::Event event;
    event.name = "event"; event.fields[0] = 0;
    project.events.push_back(event);
    auto graph = wz3::connect(project);
    check(graph.nodes[2].inputs == std::vector<std::size_t>({1, 0}), "input order");
    check(graph.nodes[3].inputs == std::vector<std::size_t>({1}), "bypass");
    check(graph.nodes[4].links[0] == 3 && !graph.nodes[4].links[1], "named links");
    check(graph.nodes[1].reachable && !graph.nodes[0].reachable && graph.nodes[6].reachable, "reachability");
    check(!graph.nodes[5].active && graph.warnings.empty(), "hidden operator");
    project.events[0].fields[10] = 1;
    project.operators[3].links = {"root"}; // cycle must terminate
    graph = wz3::connect(project);
    check(!graph.nodes[6].reachable && graph.nodes[3].reachable, "disabled event and cycle");
    project.pages[0].fields[5] = project.pages[0].fields[6] = 1;
    project.operators[6].scratch = true;
    graph = wz3::connect(project);
    check(graph.nodes[6].active && !graph.nodes[4].active, "scratch selection");
    project.operators[6].links = {"missing"};
    graph = wz3::connect(project);
    check(graph.warnings.size() == 1, "unresolved link diagnostic");
    std::cout << "spatial inputs, bypass, named links, events, scratch and cycles passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
