// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/project.hpp"
#include "wz3/graph.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "Usage: wz3-inspect PROJECT.k\n"; return 2; }
  try {
    const auto project = wz3::read_project(argv[1]);
    const auto graph = wz3::connect(project);
    const auto quote = wz3::json_string;
    std::map<std::uint32_t, unsigned> counts, reachable_counts;
    std::size_t active_count = 0, reachable_count = 0;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
      active_count += graph.nodes[i].active;
      if (graph.nodes[i].reachable) { ++reachable_count; ++reachable_counts[project.operators[i].command]; }
    }
    std::size_t animations = 0, spline_keys = 0, runtime_programs = 0;
    for (const auto& op : project.operators) { ++counts[op.command]; animations += op.animations.size(); runtime_programs += op.animation_code.size()>1; }
    for (const auto& spline : project.splines)
      for (const auto& channel : spline.channels) spline_keys += channel.size();
    const auto bpm = wz3::as_float(project.settings[4]);
    std::cout << "{\n  \"version\": " << project.version
      << ",\n  \"runtime_graph\": " << (project.runtime_graph ? "true" : "false")
      << ",\n  \"runtime_animation_programs\": " << runtime_programs
      << ",\n  \"bytes\": " << project.bytes.size()
      << ",\n  \"demo\": " << quote(project.demo_name)
      << ",\n  \"song\": " << quote(project.song_name)
      << ",\n  \"sample\": " << quote(project.sample_name)
      << ",\n  \"bpm\": " << (std::isfinite(bpm) ? std::to_string(bpm) : "null")
      << ",\n  \"operator_count\": " << project.operators.size()
      << ",\n  \"event_count\": " << project.events.size()
      << ",\n  \"animation_node_count\": " << animations
      << ",\n  \"spline_count\": " << project.splines.size()
      << ",\n  \"spline_key_count\": " << spline_keys
      << ",\n  \"blob_bytes\": " << project.blob_bytes
      << ",\n  \"active_operator_count\": " << active_count
      << ",\n  \"reachable_operator_count\": " << reachable_count << ",\n  \"entry_points\": [";
    for (std::size_t i = 0; i < graph.entry_points.size(); ++i)
      std::cout << (i ? ", " : "") << graph.entry_points[i];
    std::cout << "],\n  \"graph_warnings\": [";
    for (std::size_t i = 0; i < graph.warnings.size(); ++i)
      std::cout << (i ? ", " : "") << quote(graph.warnings[i]);
    std::cout << "],\n  \"pages\": [";
    for (std::size_t i = 0; i < project.pages.size(); ++i)
      std::cout << (i ? ", " : "") << quote(project.pages[i].name);
    std::cout << "],\n  \"inventory\": [\n";
    bool first = true;
    for (const auto& [command, count] : counts) {
      const auto info = wz3::operator_info(command);
      std::cout << (first ? "" : ",\n") << "    {\"command\": " << command
        << ", \"name\": " << quote(info.name) << ", \"category\": " << quote(info.category)
        << ", \"count\": " << count << ", \"reachable\": " << reachable_counts[command] << "}";
      first = false;
    }
    std::cout << "\n  ],\n  \"operators\": [\n";
    for (std::size_t i = 0; i < project.operators.size(); ++i) {
      const auto& op = project.operators[i];
      const auto& node = graph.nodes[i];
      std::cout << (i ? ",\n" : "") << "    {\"id\": " << i << ", \"command\": " << op.command
        << ", \"name\": " << quote(op.name) << ", \"page\": " << op.page
        << ", \"file_offset\": " << op.file_offset << ", \"blob_offset\": " << op.blob_offset
        << ", \"blob_size\": " << op.blob_size
        << ", \"active\": " << (node.active ? "true" : "false")
        << ", \"reachable\": " << (node.reachable ? "true" : "false")
        << ", \"inputs\": [";
      for (std::size_t j = 0; j < node.inputs.size(); ++j)
        std::cout << (j ? ", " : "") << node.inputs[j];
      std::cout << "], \"links\": [";
      for (std::size_t j = 0; j < node.links.size(); ++j)
        std::cout << (j ? ", " : "") << (node.links[j] ? std::to_string(*node.links[j]) : "null");
      std::cout << "], \"parameters\": [";
      for (std::size_t j = 0; j < op.parameters.size(); ++j)
        std::cout << (j ? ", " : "") << op.parameters[j];
      std::cout << "], \"strings\": [";
      for (std::size_t j = 0; j < op.strings.size(); ++j)
        std::cout << (j ? ", " : "") << quote(op.strings[j]);
      std::cout << "]}";
    }
    std::cout << "\n  ],\n  \"events\": [\n";
    for (std::size_t i=0;i<project.events.size();++i) {
      const auto& event=project.events[i];const auto found=graph.names.find(event.name);
      std::cout << (i ? ",\n" : "") << "    {\"id\": " << i << ", \"name\": " << quote(event.name)
        << ", \"operator\": " << (found==graph.names.end() ? "null" : std::to_string(found->second)) << ", \"fields\": [";
      for (std::size_t j=0;j<event.fields.size();++j)std::cout << (j ? ", " : "") << event.fields[j];
      std::cout << "], \"transform\": [";
      for (std::size_t j=0;j<event.transform.size();++j)std::cout << (j ? ", " : "") << event.transform[j];
      std::cout << "], \"interval\": [" << event.interval[0] << ", " << event.interval[1]
        << "], \"flags\": " << event.flags << ", \"spline\": " << quote(event.spline) << "}";
    }
    std::cout << "\n  ]\n}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "wz3-inspect: " << error.what() << '\n';
    return 1;
  }
}
