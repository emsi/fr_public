// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace wz3 {
struct V2Event {
  std::uint32_t tick{};
  // Repeat passes at the same tick preserve CV2MPlayer::Tick's order: one
  // program/controller/bend per pass, then all simultaneous notes per channel.
  std::uint32_t pass{};
  std::array<std::uint8_t,3> midi{};
  unsigned length{};
};
struct V2Global {
  std::uint32_t tick{},pass{},microseconds{};
  std::uint8_t numerator{},denominator{},ticks_per_quarter{};
};
struct V2Module {
  std::uint32_t time_division{},end_tick{};
  std::vector<V2Global> globals;
  std::array<std::vector<V2Event>,16> channels;
  std::vector<std::uint8_t> parameters,patches,speech;
  std::vector<std::uint32_t> patch_offsets;
};
struct V2Packet {
  std::uint64_t sample{};
  std::uint32_t tick{},pass{};
  std::vector<std::uint8_t> midi; // explicit status bytes, terminated by 0xfd
};
struct V2Sequence {
  std::vector<V2Packet> packets;
  std::uint64_t end_sample{};
};
V2Module parse_v2m(std::span<const std::uint8_t>);
V2Module read_v2m(const std::filesystem::path&);
// Original Werkkzeug3/V2 integer sample clock, including its remainder carry.
// This does not synthesize audio or imply support for the module's patch version.
V2Sequence sequence_v2m(const V2Module&,unsigned sample_rate=44100);
} // namespace wz3
