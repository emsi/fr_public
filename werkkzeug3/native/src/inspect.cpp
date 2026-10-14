// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/project.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "Usage: wz3-inspect PROJECT.k\n"; return 2; }
  try {
    const auto project = wz3::read_project(argv[1]);
    const auto quote = wz3::json_string;
    std::map<std::uint32_t, unsigned> counts;
    std::size_t animations = 0, spline_keys = 0;
    for (const auto& op : project.operators) { ++counts[op.command]; animations += op.animations.size(); }
    for (const auto& spline : project.splines)
      for (const auto& channel : spline.channels) spline_keys += channel.size();
    const auto bpm = wz3::as_float(project.settings[4]);
    std::cout << "{\n  \"version\": " << project.version
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
      << ",\n  \"blob_bytes\": " << project.blob_bytes << ",\n  \"pages\": [";
    for (std::size_t i = 0; i < project.pages.size(); ++i)
      std::cout << (i ? ", " : "") << quote(project.pages[i].name);
    std::cout << "],\n  \"inventory\": [\n";
    bool first = true;
    for (const auto& [command, count] : counts) {
      const auto info = wz3::operator_info(command);
      std::cout << (first ? "" : ",\n") << "    {\"command\": " << command
        << ", \"name\": " << quote(info.name) << ", \"category\": " << quote(info.category)
        << ", \"count\": " << count << "}";
      first = false;
    }
    std::cout << "\n  ],\n  \"operators\": [\n";
    for (std::size_t i = 0; i < project.operators.size(); ++i) {
      const auto& op = project.operators[i];
      std::cout << (i ? ",\n" : "") << "    {\"id\": " << i << ", \"command\": " << op.command
        << ", \"name\": " << quote(op.name) << ", \"page\": " << op.page
        << ", \"file_offset\": " << op.file_offset << ", \"blob_offset\": " << op.blob_offset
        << ", \"blob_size\": " << op.blob_size << ", \"parameters\": [";
      for (std::size_t j = 0; j < op.parameters.size(); ++j)
        std::cout << (j ? ", " : "") << op.parameters[j];
      std::cout << "], \"strings\": [";
      for (std::size_t j = 0; j < op.strings.size(); ++j)
        std::cout << (j ? ", " : "") << quote(op.strings[j]);
      std::cout << "]}";
    }
    std::cout << "\n  ]\n}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "wz3-inspect: " << error.what() << '\n';
    return 1;
  }
}
