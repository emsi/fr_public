// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
  bool rejected = false;
  try { action(); } catch (const std::runtime_error&) { rejected = true; }
  check(rejected, "invalid cached font accepted");
}
void word(std::vector<std::uint8_t>& data, std::size_t at, int value) {
  data.at(at) = value & 255; data.at(at+1) = (value >> 8) & 255;
}
wz3::Texture blank(unsigned size) {
  wz3::Texture t; t.width = t.height = size;
  t.pixels.resize(size * size, wz3::color64(0xff000000)); return t;
}
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }

void synthetic() {
  constexpr auto header = 8 + 256 * 10;
  std::vector<std::uint8_t> blob(header + 16);
  blob[0] = 3; blob[1] = 1;
  word(blob, 2, 4); word(blob, 4, 4); word(blob, 6, 4);
  const auto a = 8 + 'A' * 10;
  word(blob, a, 4); word(blob, a+2, 8); word(blob, a+4, -2);
  word(blob, a+6, 3); word(blob, a+8, 1);
  for (unsigned i = 0; i < 16; ++i) blob[header + i] = i % 8;
  auto t = blank(4);
  wz3::draw_cached_text(t, blob, 0xffffffff, 0x90, 1.f/16, "saved font");
  check(bool(t.fonts[1]) && !t.fonts[0], "cached page selection");
  const auto& atlas = *t.fonts[1];
  check(atlas.file.empty() && atlas.family == "saved font" &&
        atlas.rasterizer == "Werkkzeug3 cached Text v3", "cached font provenance");
  const auto& g = atlas.glyphs['A'];
  check(g.present && g.uv == std::array<float,4>{.25f,.5f,9.f/16,14.f/16} &&
        g.pre_space == -3.f/16 && g.advance == 4.f/16, "cached signed bearings/spacing");
  const auto& absent = atlas.glyphs['Z'];
  check(!absent.present && absent.uv == std::array<float,4>{0,0,.125f,.375f} &&
        absent.pre_space == -1.f/16 && absent.advance == .125f, "absent character metrics");
  check(t.pixels[0][0] == 0 && t.pixels[7][0] == 32766, "cached coverage endpoints");
  for (unsigned i = 1; i < 8; ++i) check(t.pixels[i][0] > t.pixels[i-1][0], "coverage ordering");

  // Exercise the public evaluator through Text, Blur, Merge and Store without
  // any font file. The cached path must win even if a nonexistent override is supplied.
  wz3::Project p; p.pages.resize(1); p.bytes = blob;
  wz3::Operator flat; flat.command=0x21; flat.width=1; flat.parameters={2,2,0xff000000};
  p.operators.push_back(flat);
  wz3::Operator text; text.command=0x31; text.width=1; text.y=1;
  text.parameters={0,0,bits(.25f),0,0xffffffff,0x90,2,bits(1.f/16)};
  text.strings={"A", "saved font"}; text.blob_size=blob.size(); p.operators.push_back(text);
  wz3::Operator blur; blur.command=0x29; blur.width=1; blur.y=2;
  blur.parameters={0,0,0,bits(1)}; p.operators.push_back(blur);
  wz3::Operator merge; merge.command=0x24; merge.width=1; merge.y=3;
  merge.parameters={4}; p.operators.push_back(merge);
  wz3::Operator store; store.command=2; store.width=1; store.y=4; store.name="root";
  p.operators.push_back(store);
  auto graph=wz3::connect(p); graph.nodes[3].inputs={1,2};
  const auto texture=wz3::evaluate_texture(p, graph, 4, {"/no-host-font.ttf"});
  check(texture->fonts[1] && texture->fonts[1]->glyphs['A'].uv == g.uv,
        "cached metrics lost through texture graph");

  for (unsigned page=1;page<=7;++page) {
    auto target=blank(4); wz3::draw_cached_text(target, blob, 0xffffffff, 0x80 | page<<4);
    check(target.fonts[page] && target.fonts[page]->page == page, "cached atlas page range");
  }
  const auto invalid = [&](auto change) {
    auto damaged=blob; change(damaged); auto target=blank(4); const auto previous=target.pixels;
    rejects([&] { wz3::draw_cached_text(target, damaged, 0xffffffff, 0x90); });
    check(target.pixels == previous && !target.fonts[1], "failed decode changed texture");
  };
  invalid([](auto& b){b.resize(7);});
  invalid([](auto& b){b.pop_back();});
  invalid([](auto& b){b.push_back(0);});
  invalid([](auto& b){b[0]=2;});
  invalid([](auto& b){b[1]=0;});
  invalid([](auto& b){b[2]=8;});
  invalid([](auto& b){b[6]=0;});
  invalid([](auto& b){b.back()=8;});
  invalid([&](auto& b){word(b,a+6,-1);});
  rejects([&]{wz3::draw_cached_text(t,blob,0xffffffff,0x90,std::numeric_limits<float>::quiet_NaN());});
  rejects([&]{wz3::draw_cached_text(t,blob,0xffffffff,0x90,-.1f);});
  rejects([&]{wz3::draw_cached_text(t,blob,0xffffffff,0x10);});
}

std::vector<std::uint8_t> read_blob(const char* path) {
  std::ifstream input(path,std::ios::binary|std::ios::ate);
  check(bool(input),"cannot open release font blob"); const auto size=input.tellg();
  check(size==264712,"unexpected release font blob size");
  std::vector<std::uint8_t> result(static_cast<std::size_t>(size)); input.seekg(0);
  check(bool(input.read(reinterpret_cast<char*>(result.data()),size)),"cannot read release font blob");
  return result;
}
void release_fonts(const char* duality, const char* droid) {
  auto t=blank(512); const auto blob=read_blob(duality);
  wz3::draw_cached_text(t,blob,0xffffffff,0x90,1.f/256,"duality");
  const auto& atlas=*t.fonts[1];
  check(std::count_if(atlas.glyphs.begin(),atlas.glyphs.end(),[](auto& g){return g.present;})==73,
        "released duality glyph count");
  const auto& j=atlas.glyphs['j'];
  check(j.uv==std::array<float,4>{1172.f/2048,16.f/2048,1262.f/2048,312.f/2048} &&
        j.pre_space==-19.f/2048 && j.advance==84.f/2048,"released duality metric values");
  check(std::count_if(t.pixels.begin(),t.pixels.end(),[](auto& p){return p[0]!=0;})>10000,
        "released font mask is blank");
  t=blank(512); const auto title=read_blob(droid);
  wz3::draw_cached_text(t,title,0xffffffff,0xa0,36.f/4096,"Droid");
  const auto& heading=*t.fonts[2];
  check(std::count_if(heading.glyphs.begin(),heading.glyphs.end(),[](auto& g){return g.present;})==4,
        "released Droid glyph count");
  check(heading.glyphs['h'].uv[3]==2086.f/2048,"released atlas UV outside texture was clamped");
  std::cout<<"Released duality and Droid masks/metrics passed without a host font\n";
}
}
int main(int argc,char** argv) {
  try {
    check(argc==1 || argc==3,"expected optional duality and Droid blob paths");
    synthetic();
    if(argc==3)release_fonts(argv[1],argv[2]);
    std::cout<<"Cached font atlas graph, metrics, coverage and invalid-input checks passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
