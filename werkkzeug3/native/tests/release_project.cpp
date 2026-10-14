// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/project.hpp"
#include "wz3/graph.hpp"
#include "wz3/animation.hpp"
#include <bit>
#include <cfenv>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f) {
  bool failed=false;try{f();}catch(const std::runtime_error&){failed=true;}
  check(failed,"invalid runtime project/program accepted");
}
struct Writer {
  std::vector<std::uint8_t> data;
  void byte(unsigned value){data.push_back(value);}
  void number(unsigned value,unsigned size=4){for(unsigned i=0;i<size;++i)byte(value>>(8*i));}
  void string(std::string_view value){data.insert(data.end(),value.begin(),value.end());byte(0);}
};
void replace_word(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)bytes.at(at+i)=value>>(8*i);
}
std::vector<std::uint8_t> fixture() {
  Writer w;w.string("WZ3THR1");w.number(800);w.number(600);w.number(std::bit_cast<unsigned>(138.f));
  w.number(609u<<16);w.number(0);w.number(0);
  w.byte(2);w.byte(1);w.byte(1);for(unsigned i=1;i<16;++i)w.byte(2);
  w.number(0x80003);w.byte(0x21);w.string("bbc"); // three fields and a blob
  w.number(0x111101);w.byte(0xc3);w.string("f"); // parameter/input/link/string/spline
  w.number(0);
  w.byte(0);w.byte(0x81); // second node consumes first, previous-op shortcut
  w.byte(1); // second node also links to the first
  w.byte(2);w.byte(2);w.number(0xff000000);w.number(4); // first parameters, blob size
  w.byte(0x7f);w.byte(0x99);w.byte(0x19);w.string("runtime");w.number(1,2);
  w.byte(1); // first animation: END
  for(auto v:{2,0,0x19,0,0,0x91,0,1})w.byte(v); // spline(time) -> parameter 0
  w.byte(1);w.byte(1);w.number(0);w.number(65536);w.byte(1);w.byte(0);w.byte(7);
  for(auto v:{1,1,1,0,0,0,0,0,0})w.byte(v);
  w.number(0xff123456);w.byte(1);w.byte(0);w.byte(128);
  w.byte(9);w.byte(2);w.byte(0);w.byte(0);w.byte(255);w.byte(128); // linear 0..1 spline
  for(auto v:{'W','X','Y','Z'})w.byte(v);
  replace_word(w.data,28,w.data.size()-32);return w.data;
}
void test_project() {
  const auto data=fixture();const auto p=wz3::parse_project(data);const auto g=wz3::connect(p);
  check(p.runtime_graph && p.version==1 && p.operators.size()==2 && p.events.size()==1 && p.splines.size()==1,
        "runtime project inventory");
  check(g.names.at("root")==1 && g.nodes[1].inputs==std::vector<std::size_t>{0} && g.nodes[1].links[0]==0 &&
        g.nodes[0].reachable && g.nodes[1].reachable && g.warnings.empty(),"explicit runtime graph lost references");
  check(p.operators[1].parameters==std::vector<std::uint32_t>{0x3f999900} &&
        p.operators[1].strings==std::vector<std::string>{"runtime"},"packed payload changed");
  check(p.operators[0].blob_size==4 && p.bytes[p.operators[0].blob_offset]=='W' && p.blob_bytes==4,"runtime blob offsets");
  check(p.events[0].name==p.operators[1].name && p.events[0].fields[3]==7 &&
        p.events[0].fields[4]==0x3f800000 && p.events[0].fields[9]==0xff123456 &&
        p.events[0].interval[1]==0x3f800000,"runtime event layout");
  const wz3::SplineLibrary splines(p);check(splines.find(p.events[0].spline)==0,"runtime event spline");
  const auto program=wz3::compile_animation(p.operators[1],splines);
  check(program.code==p.operators[1].animation_code && program.read_variables==1,"runtime bytecode was recompiled");
  auto env=wz3::frame_environment(32768);
  const wz3::AnimationScope scope(program,splines,p.operators[1].parameters,env);
  check(scope.parameters==std::vector<std::uint32_t>{0x3f000000},"runtime spline animation execution");
  for(std::size_t size=0;size<data.size();++size) {
    auto truncated=data;truncated.resize(size);
    if(size>=32)replace_word(truncated,28,size-32);
    rejects([&]{wz3::parse_project(truncated);});
  }
  auto bad=data;bad.push_back(0);replace_word(bad,28,bad.size()-32);
  rejects([&]{wz3::parse_project(bad);});
  for(auto at:{8,12,16,20}) {bad=data;replace_word(bad,at,0);rejects([&]{wz3::parse_project(bad);});}
  bad=data;replace_word(bad,24,2);rejects([&]{wz3::parse_project(bad);});
  bad=data;bad[34]=3;rejects([&]{wz3::parse_project(bad);});
  bad=data;replace_word(bad,24,3);rejects([&]{wz3::parse_project(bad);}); // wrong verified endpoint
  replace_word(bad,20,592u<<16);const auto verified=wz3::parse_project(bad);
  check(verified.release_timing && verified.settings[9]==1,"verified clock profile flag");
  replace_word(bad,16,std::bit_cast<unsigned>(139.f));rejects([&]{wz3::parse_project(bad);});

}
void test_bytecode() {
  const auto p=wz3::parse_project(fixture());const wz3::SplineLibrary splines(p);
  auto op=p.operators[1];
  const std::vector<std::vector<std::uint8_t>> invalid{
    {2}, {2,32,0x91,0,1}, {0x91,0,1}, {6,0,0x91,0,1}, {2,0,0x91,1,1},
    {2,0,0x19,1,0,0x91,0,1}, {0xc0,0,1}, {1,0}, {2,0,1},
    {0x18,0,0,0x80,0x7f,0x91,0,1}, {0x0b,1}
  };
  for(const auto& code:invalid) {op.animation_code=code;rejects([&]{wz3::compile_animation(op,splines);});}
  op.animation_code={1};op.animations.resize(1);rejects([&]{wz3::compile_animation(op,splines);});
}
void test_release_rounding() {
  const wz3::SplineLibrary splines(wz3::Project{});
  wz3::Operator op;op.parameters.resize(29);op.animation_code={2,0,0xbf,28,1};
  const auto program=wz3::compile_animation(op,splines);
  check(program.release_profile,"released bytecode lost its arithmetic profile");
  // Actual title material program from final 1.01: all four bytes follow time.
  const auto old=std::fegetround();std::fesetround(FE_DOWNWARD);
  for(const auto& [time,expected]:std::vector<std::pair<float,unsigned>>{
      {-.25f,0},{.25f,0x40404040},{.5f,0x80808080},{.75f,0xbfbfbfbf},{1.25f,0xffffffff}}) {
    auto env=wz3::frame_environment(0);env.variables[0].fill(time);
    const wz3::AnimationScope result(program,splines,op.parameters,env);
    check(result.parameters[28]==expected,"release fade must round to nearest even");
    check(std::fegetround()==FE_DOWNWARD,"release animation changed caller rounding");
  }
  std::fesetround(old);
  op.animation_code={2,0,0xe1,0,1};op.parameters[0]=2;
  for(const auto& [value,expected]:std::vector<std::pair<float,int>>{{2.5f,2},{3.5f,4},{-2.5f,-2},{-3.5f,-4}}) {
    auto env=wz3::frame_environment(0);env.variables[0].fill(value);
    const wz3::AnimationScope result(wz3::compile_animation(op,splines),splines,op.parameters,env);
    check(result.parameters[0]==std::uint32_t(expected) && result.changed,"release integer conversion/change flag");
  }
  op.animation_code={2,0,0xf5,0,1};op.parameters[0]=0xaabbccdd;
  auto env=wz3::frame_environment(32768);const std::vector<std::uint32_t> previous(29,0xaa80cc80);
  const wz3::AnimationScope masked(wz3::compile_animation(op,splines),splines,op.parameters,env,previous);
  check(masked.parameters[0]==0xaa80cc80 && !masked.changed,"release byte mask/previous-state comparison");
}
}
int main() {
  try{test_project();test_bytecode();test_release_rounding();std::cout<<"Runtime graph, packed values, event/spline execution and malformed-input checks passed\n";}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
