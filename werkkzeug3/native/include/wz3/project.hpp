// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wz3 {

struct Page {
  std::array<std::uint32_t, 8> fields{};
  std::string name, owner, next_owner;
};

struct Animation {
  std::array<std::uint32_t, 8> fields{};
  std::vector<std::uint32_t> values;
  std::string name;
};

struct Operator {
  std::size_t file_offset{};
  std::uint32_t command{}, page{}, convention{};
  std::int32_t x{}, y{}, width{};
  bool scratch{}, bypass{}, hidden{};
  std::string name;
  std::vector<std::string> links, strings, splines;
  std::vector<std::uint32_t> parameters;
  std::vector<Animation> animations;
  std::vector<std::uint8_t> animation_code; // compiled runtime graph; includes KA_END
  bool has_animation() const { return !animations.empty() || animation_code.size() > 1; }
  std::uint32_t blob_size{};
  std::size_t blob_offset{};
};

struct Event {
  // Original operator index, start/end in 16.16 beats, selection, velocity,
  // modulation, editor line/selection, mark, color, disabled, reserved.
  std::array<std::uint32_t, 12> fields{};
  std::string name, spline;
  std::array<std::uint32_t, 9> transform{};
  std::array<std::uint32_t, 2> interval{};
  std::uint32_t flags{};
};

struct Spline {
  std::uint32_t page{}, kind{}, interpolation{};
  std::string name;
  // Preserve IEEE-754 bits; decoding must not change the saved recipe.
  std::vector<std::vector<std::array<std::uint32_t, 2>>> channels;
};

struct Project {
  std::vector<std::uint8_t> bytes;
  std::uint32_t version{};
  bool runtime_graph{}, release_timing{}; // final 1.01 clock profile recovered from the executable
  std::vector<std::vector<std::size_t>> runtime_inputs;
  std::array<std::size_t, 16> runtime_roots{};
  std::vector<Page> pages;
  std::vector<Operator> operators;
  std::vector<Event> events;
  std::vector<Spline> splines;
  std::array<std::uint32_t, 16> settings{};
  std::string demo_name, song_name, sample_name;
  std::size_t blob_bytes{};
};

struct OperatorInfo { std::string_view name, category; };
OperatorInfo operator_info(std::uint32_t command);
Project read_project(const std::filesystem::path& path);
Project parse_project(std::vector<std::uint8_t> bytes);
// Native envelope around the original Theta release graph, with explicit timing.
Project parse_release_project(std::vector<std::uint8_t> bytes);
// Original project strings are byte strings. JSON escapes bytes >= 128 as
// U+00xx, giving a lossless Latin-1 representation rather than invalid UTF-8.
std::string json_string(std::string_view text);
float as_float(std::uint32_t bits);

} // namespace wz3
