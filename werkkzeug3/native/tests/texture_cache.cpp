// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <algorithm>
#include <iostream>

template<class F> void rejects(F action) {
  bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}
  check(rejected,"invalid texture graph accepted");
}
void long_recipe() {
  FrameFixture fixture;auto id=fixture.add(0x21,{9,9,0xff123456});
  // Keeping all 171 images would need 342 MiB and exceed the 256 MiB limit.
  // Format changes copy pixels but leave them exact and avoid expensive filters.
  for(unsigned i=0;i<170;++i)id=fixture.add(0x25,{1,i+1,i&31},{id});
  id=fixture.add(3,{}, {id});id=fixture.add(1,{}, {},{id});
  wz3::TextureEvaluationStats stats;
  const auto output=wz3::evaluate_texture(fixture.p,fixture.g,id,{},&stats);
  check(output->pixels.size()==512*512 && std::all_of(output->pixels.begin(),output->pixels.end(),[](auto p){return p==wz3::color64(0xff123456);}),"long recipe changed pixels");
  check(output->mip_count==170 && output->mip_threshold==(169&31),"long recipe lost metadata");
  check(stats.operators==173 && stats.allocations==171,"recipe evaluated more than once");
  check(stats.peak_pixel_bytes==4*1024*1024,"dead intermediates retained");
  std::cout<<"342 MiB cumulative texture recipe used "<<stats.peak_pixel_bytes/(1024*1024)<<" MiB peak pixel storage\n";
}
void shared_branches() {
  FrameFixture fixture;const auto flat=fixture.add(0x21,{4,3,0xff102030});
  const auto shared=fixture.add(0x23,{1,0x00112233},{flat});
  const auto store=fixture.add(2,{}, {shared}),load=fixture.add(1,{}, {},{store});
  const auto left=fixture.add(0x23,{4,0},{shared}),right=fixture.add(0x23,{0,0x80906030},{load});
  const auto root=fixture.add(0x24,{0},{left,right,shared,shared});
  // An unreachable malformed branch must not affect this recipe's consumers.
  fixture.add(1);
  wz3::TextureEvaluationStats stats;
  auto output=wz3::evaluate_texture(fixture.p,fixture.g,root,{},&stats);
  const auto base=wz3::color_pixel(wz3::color64(0xff102030),wz3::color64(0x00112233),1);
  auto expected=wz3::merge_pixel(wz3::color_pixel(base,{},4),wz3::color_pixel(base,wz3::color64(0x80906030),0),0);
  expected=wz3::merge_pixel(wz3::merge_pixel(expected,base,0),base,0);
  check(std::all_of(output->pixels.begin(),output->pixels.end(),[&](auto p){return p==expected;}),"shared/duplicate/aliased input released early");
  check(stats.operators==7 && stats.allocations==5,"shared branch regenerated");
  check(stats.peak_pixel_bytes==4*128*sizeof(wz3::Pixel),"shared storage counted more than once");
  // A new evaluation must be independent, while an older result stays alive.
  auto again=wz3::evaluate_texture(fixture.p,fixture.g,root);
  check(again->pixels==output->pixels,"result did not survive evaluator lifetime");
  fixture.g.nodes[shared].inputs={root};rejects([&]{wz3::evaluate_texture(fixture.p,fixture.g,root);});
  fixture.g.nodes[shared].inputs={999};rejects([&]{wz3::evaluate_texture(fixture.p,fixture.g,root);});
  fixture.g.nodes.pop_back();rejects([&]{wz3::evaluate_texture(fixture.p,fixture.g,root);});
}
int main() {
  try{long_recipe();shared_branches();std::cout<<"Texture lifetimes, aliases, shared branches and malformed graph checks passed\n";}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
