// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Format reference: WerkDoc::Read/Write in ../werkkzeug.cpp and
// sReadBegin/sReadString/sReadEnd in ../_types.cpp.
#include "wz3/project.hpp"

#include <bit>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

namespace wz3 {
namespace {
constexpr std::size_t max_file_size = 64 * 1024 * 1024;

class Reader {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
  std::size_t offset() const { return pos_; }
  std::size_t remaining() const { return bytes_.size() - pos_; }
  [[noreturn]] void fail(std::string_view reason) const {
    throw std::runtime_error("at byte " + std::to_string(pos_) + ": " + std::string(reason));
  }
  void require(std::size_t count) const {
    if (count > remaining()) fail("truncated project");
  }
  std::uint32_t word() {
    require(4);
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes_[pos_++]) << (i * 8);
    return value;
  }
  template<std::size_t N> std::array<std::uint32_t, N> words() {
    require(N * 4);
    std::array<std::uint32_t, N> result{};
    for (auto& value : result) value = word();
    return result;
  }
  void expect(std::uint32_t value, std::string_view reason) {
    if (word() != value) fail(reason);
  }
  // Counts are checked before allocation. Limits exceed Theta's data but
  // bound work and allocation on damaged or deliberately hostile input.
  std::uint32_t count(std::size_t minimum_bytes, std::uint32_t maximum) {
    const auto value = word();
    if (value > maximum || value > remaining() / minimum_bytes) fail("invalid record count");
    return value;
  }
  void skip(std::size_t bytes) { require(bytes); pos_ += bytes; }
  void blob(std::uint32_t bytes) { skip((std::size_t(bytes) + 3) & ~std::size_t(3)); }
  std::string string(std::uint32_t maximum = 16383) {
    const auto size = word();
    if (size > maximum) fail("string exceeds format limit");
    require(size);
    std::string result(reinterpret_cast<const char*>(bytes_.data() + pos_), size);
    if (result.find('\0') != std::string::npos) fail("embedded NUL in string");
    blob(size);
    return result;
  }
private:
  std::span<const std::uint8_t> bytes_;
  std::size_t pos_{};
};
}

float as_float(std::uint32_t bits) { return std::bit_cast<float>(bits); }

OperatorInfo operator_info(std::uint32_t command) {
  static const std::map<std::uint32_t, OperatorInfo> catalog{
#include "catalog.inc"
  };
  const auto found = catalog.find(command);
  return found == catalog.end() ? OperatorInfo{"Unknown", "UNKNOWN"} : found->second;
}

Project read_project(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("cannot open project: " + path.string());
  const auto size = input.tellg();
  if (size < 0 || size > static_cast<std::streamoff>(max_file_size))
    throw std::runtime_error("project exceeds 64 MiB limit");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
    throw std::runtime_error("cannot read project: " + path.string());
  return parse_project(std::move(bytes));
}

Project parse_project(std::vector<std::uint8_t> bytes) {
  if (bytes.size() > max_file_size) throw std::runtime_error("project exceeds 64 MiB limit");
  Project project;
  project.bytes = std::move(bytes);
  Reader reader(project.bytes);
  reader.expect(0x3c3c3c3c, "incorrect project magic");
  reader.expect(0x01000100, "not a Werkkzeug3 document");
  project.version = reader.word();
  if (project.version != 13 && project.version != 14)
    reader.fail("supported project versions are 13 and 14");
  const auto body_words = reader.word();
  if (reader.remaining() < 4 || std::size_t(body_words) * 4 != reader.remaining() - 4)
    reader.fail("declared project size does not match file");
  const auto event_count = reader.count(64, 100000);
  const auto page_count = reader.count(44, 4096);
  const auto operator_count = reader.count(56, 100000);
  reader.expect(0, "obsolete animation section is not supported");
  reader.skip(16);

  for (std::uint32_t i = 0; i < page_count; ++i) {
    Page page;
    page.fields = reader.words<8>();
    page.name = reader.string(31);
    page.owner = reader.string(31);
    page.next_owner = reader.string(31);
    if (page.fields[6] && !page.fields[5]) reader.fail("active scratch page has no scratch list");
    project.pages.push_back(std::move(page));
  }

  for (std::uint32_t i = 0; i < operator_count; ++i) {
    Operator op;
    op.file_offset = reader.offset();
    const auto fields = reader.words<13>();
    op.command = fields[0];
    op.x = std::bit_cast<std::int32_t>(fields[2]);
    op.y = std::bit_cast<std::int32_t>(fields[3]);
    op.width = std::bit_cast<std::int32_t>(fields[4]);
    op.page = fields[5];
    op.convention = fields[6];
    op.scratch = fields[7] != 0;
    op.bypass = fields[8] != 0;
    const auto string_count = fields[9];
    const auto animation_count = fields[10];
    op.hidden = fields[11] != 0;
    op.blob_size = fields[12];
    if (op.page >= page_count) reader.fail("operator has invalid page index");
    if (op.width <= 0 || op.width > 65536) reader.fail("invalid operator width");
    if (op.scratch && !project.pages[op.page].fields[5]) reader.fail("operator has no scratch list");
    if (string_count > 7 || animation_count > reader.remaining() / 32)
      reader.fail("invalid operator payload count");
    op.name = reader.string(31);
    for (unsigned j = 0; j < ((op.convention >> 12) & 15); ++j) op.links.push_back(reader.string(31));
    for (unsigned j = 0; j < (op.convention & 255); ++j) op.parameters.push_back(reader.word());
    for (unsigned j = 0; j < string_count; ++j) op.strings.push_back(reader.string());
    for (unsigned j = 0; j < ((op.convention >> 20) & 7); ++j) op.splines.push_back(reader.string(31));
    for (unsigned j = 0; j < animation_count; ++j) {
      Animation animation;
      animation.fields = reader.words<8>();
      const auto kind = animation.fields[5];
      unsigned values = 0;
      switch (kind) {
      case 0: case 5: break;
      case 1: values = 4; break;
      case 2: case 3: values = 1; break;
      case 4: animation.name = reader.string(31); break;
      case 6: case 7: values = 2; break;
      default: reader.fail("unknown animation union kind");
      }
      for (unsigned k = 0; k < values; ++k) animation.values.push_back(reader.word());
      op.animations.push_back(std::move(animation));
    }
    project.operators.push_back(std::move(op));
  }

  for (std::uint32_t i = 0; i < event_count; ++i) {
    Event event;
    event.fields = reader.words<12>();
    if (event.fields[0] != 0xffffffff && event.fields[0] >= operator_count)
      reader.fail("event has invalid operator index");
    event.name = reader.string(31);
    event.transform = reader.words<9>();
    event.spline = reader.string(31);
    event.interval = reader.words<2>();
    if (project.version >= 14) event.flags = reader.word();
    project.events.push_back(std::move(event));
  }

  for (std::uint32_t i = 0; i < page_count; ++i) {
    const auto count = reader.count(20, 100000);
    for (std::uint32_t j = 0; j < count; ++j) {
      Spline spline;
      spline.page = i;
      spline.kind = reader.word();
      const auto channels = reader.count(8, 64);
      spline.interpolation = reader.word();
      reader.skip(4);
      spline.name = reader.string(31);
      for (std::uint32_t k = 0; k < channels; ++k) {
        const auto keys = reader.count(8, 1000000);
        reader.skip(4);
        std::vector<std::array<std::uint32_t, 2>> channel;
        channel.reserve(keys);
        for (std::uint32_t l = 0; l < keys; ++l) channel.push_back(reader.words<2>());
        spline.channels.push_back(std::move(channel));
      }
      project.splines.push_back(std::move(spline));
    }
  }

  reader.expect(0xdeadbeef, "missing blob section start");
  for (auto& op : project.operators) {
    op.blob_offset = reader.offset();
    reader.blob(op.blob_size);
    project.blob_bytes += op.blob_size;
  }
  reader.expect(0xdeadbeef, "missing blob section end");
  project.settings = reader.words<16>();
  project.demo_name = reader.string();
  project.song_name = reader.string();
  project.sample_name = reader.string();
  reader.expect(0x3e3e3e3e, "missing document end");
  if (reader.remaining()) reader.fail("unexpected trailing data");
  return project;
}

std::string json_string(std::string_view text) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 32 || c >= 127) {
      result += "\\u00";
      result += hex[c >> 4]; result += hex[c & 15];
    } else result += char(c);
  }
  return result + '"';
}
} // namespace wz3
