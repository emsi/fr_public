// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <jpeglib.h>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
  bool rejected = false;
  try { action(); } catch (const std::runtime_error&) { rejected = true; }
  check(rejected, "invalid embedded asset was accepted");
}
std::vector<std::uint8_t> test_jpeg() {
  jpeg_compress_struct encoder{};
  jpeg_error_mgr error{};
  encoder.err = jpeg_std_error(&error);
  jpeg_create_compress(&encoder);
  unsigned char* memory = nullptr;
  unsigned long length = 0;
  jpeg_mem_dest(&encoder, &memory, &length);
  encoder.image_width = 10; encoder.image_height = 6;
  encoder.input_components = 1; encoder.in_color_space = JCS_GRAYSCALE;
  jpeg_set_defaults(&encoder);
  jpeg_set_quality(&encoder, 100, TRUE);
  jpeg_start_compress(&encoder, TRUE);
  while (encoder.next_scanline < encoder.image_height) {
    unsigned char row[10];
    for (unsigned x = 0; x < 10; ++x) row[x] = encoder.next_scanline * 40 + x * 3;
    JSAMPROW pointer = row;
    jpeg_write_scanlines(&encoder, &pointer, 1);
  }
  jpeg_finish_compress(&encoder);
  const std::vector<std::uint8_t> result(memory, memory + length);
  std::free(memory);
  jpeg_destroy_compress(&encoder);
  return result;
}
void text_masks() {
  wz3::Texture texture;
  texture.width = 8; texture.height = 1; texture.pixels.resize(8, wz3::color64(0xff000000));
  const auto original = texture;
  std::vector<std::uint8_t> mask{2, 0, 8, 0, 1, 0, 4, 0, 0, 1, 2, 3, 4, 5, 6, 7};
  wz3::draw_cached_text(texture, mask, 0xffffffff, 128);
  // v2 coverage maps to eight intensities; Fade64 deliberately loses low bits.
  const std::uint16_t expected[] = {0, 4624, 9378, 14004, 18760, 23386, 28140, 32766};
  for (unsigned i = 0; i < 8; ++i) check(texture.pixels[i][0] == expected[i], "cached mask coverage conversion");
  auto version3 = original; mask[0] = 3;
  wz3::draw_cached_text(version3, mask, 0xffffffff, 128);
  check(texture.pixels == version3.pixels, "v2/v3 cached mask compatibility");
  auto invalid = mask; invalid.pop_back();
  rejects([&] { wz3::draw_cached_text(texture, invalid, 0xffffffff, 128); });
  invalid = mask; invalid[8] = 8;
  rejects([&] { wz3::draw_cached_text(texture, invalid, 0xffffffff, 128); });
  invalid = mask; invalid[0] = 4;
  rejects([&] { wz3::draw_cached_text(texture, invalid, 0xffffffff, 128); });
  rejects([&] { wz3::draw_cached_text(texture, mask, 0xffffffff, 0); });
  rejects([&] { wz3::draw_cached_text(texture, mask, 0xffffffff, 144); });
}
void jpeg_crop() {
  wz3::Project project; project.pages.resize(1); project.bytes = test_jpeg();
  wz3::Operator op; op.command = 0x3a; op.width = 1; op.blob_size = project.bytes.size();
  project.operators.push_back(op);
  const auto graph = wz3::connect(project);
  const auto texture = wz3::evaluate_texture(project, graph, 0);
  check(texture->width == 8 && texture->height == 4, "JPEG import must crop to lower powers of two");
  for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 8; ++x) {
    const auto& pixel = texture->pixels[y * 8 + x];
    const auto expected = int(((y * 40 + x * 3) * 257) >> 1);
    check(std::abs(int(pixel[0]) - expected) <= 129, "JPEG crop/orientation differs from source");
    check(pixel[0] == pixel[1] && pixel[1] == pixel[2] && pixel[3] == 32767, "JPEG grayscale/alpha conversion");
  }
  for (auto length : {std::size_t(2), std::size_t(16), project.bytes.size()/2, project.bytes.size()-8}) {
    project.operators[0].blob_size = length;
    rejects([&] { wz3::evaluate_texture(project, graph, 0); });
  }
  project.operators[0].blob_size = project.bytes.size();
  for (std::size_t pos = 2; pos + 8 < project.bytes.size();) {
    check(project.bytes[pos] == 0xff, "unexpected test JPEG marker");
    const auto marker = project.bytes[pos+1];
    if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
      project.bytes[pos+7] = 0x20; project.bytes[pos+8] = 0; // 8192 pixels wide
      rejects([&] { wz3::evaluate_texture(project, graph, 0); });
      return;
    }
    pos += 2 + (unsigned(project.bytes[pos+2]) << 8) + project.bytes[pos+3];
  }
  throw std::runtime_error("test JPEG frame header not found");
}
}
int main(int argc, char** argv) {
  try {
    check(argc == 2, "expected Theta fixture path");
    text_masks(); jpeg_crop();
    const auto project = wz3::read_project(argv[1]);
    const auto graph = wz3::connect(project);
    for (const auto* name : {"tex.BWEye", "tex.intro1", "tex.intro2", "tex.intro3", "tex.intro4", "tex.intro5"}) {
      const auto texture = wz3::evaluate_texture(project, graph, graph.names.at(name));
      check(!texture->pixels.empty(), "empty embedded Theta texture");
    }
    rejects([&] { wz3::evaluate_texture(project, graph, graph.names.at("font_bitmap")); });
    std::cout << "embedded assets, crop behavior and malformed-data rejection passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
