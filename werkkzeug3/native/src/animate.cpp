// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/animation.hpp"
#include "wz3/graph.hpp"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
template<class T> T number(std::string_view text) {
  T value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if(error!=std::errc{} || end!=text.data()+text.size())throw std::runtime_error("invalid numeric argument");
  return value;
}
void real(float value) {if(std::isfinite(value))std::cout<<value;else std::cout<<"null";}
}
int main(int argc,char** argv) {
  if(argc!=3 && argc!=4 && argc!=5) {
    std::cerr<<"Usage: wz3-animate PROJECT.k --audit\n       wz3-animate PROJECT.k OPERATOR_NAME_OR_ID BEATS [EVENT_INDEX]\n";return 2;
  }
  try {
    const auto project=wz3::read_project(argv[1]);const auto graph=wz3::connect(project);const wz3::SplineLibrary splines(project);
    std::cout<<std::setprecision(9);
    if(std::string_view(argv[2])=="--audit" && argc==3) {
      std::size_t programs=0,reachable=0,nodes=0,used=0,bytes=0,evaluations=0,samples=0;
      for(std::size_t id=0;id<project.operators.size();++id) {
        const auto& op=project.operators[id];if(!op.has_animation())continue;
        try {
          const auto program=wz3::compile_animation(op,splines);++programs;reachable+=graph.nodes[id].reachable;
          nodes+=program.source_nodes;used+=program.used_nodes;bytes+=program.code.size();
          for(auto beat:{0,16384,32768,49152,65536}) {
            auto env=wz3::frame_environment(beat);const wz3::AnimationScope result(program,splines,op.parameters,env);++evaluations;
          }
        }catch(const std::exception& error) {throw std::runtime_error("operator "+std::to_string(id)+": "+error.what());}
      }
      for(std::size_t i=0;i<splines.size();++i)for(unsigned t=0;t<=256;++t) {
        const auto value=splines.evaluate(i,t/256.0f);for(auto v:value)if(!std::isfinite(v))throw std::runtime_error("nonfinite spline sample");++samples;
      }
      std::cout<<"{\"programs\":"<<programs<<",\"reachable_programs\":"<<reachable<<",\"source_nodes\":"<<nodes
        <<",\"used_nodes\":"<<used<<",\"bytecode_bytes\":"<<bytes<<",\"program_evaluations\":"<<evaluations
        <<",\"splines\":"<<splines.size()<<",\"spline_samples\":"<<samples<<"}\n";
    }else {
      if(argc<4)throw std::runtime_error("expected an operator and beat position");
      const auto found=graph.names.find(argv[2]);const auto id=found==graph.names.end() ? number<std::size_t>(argv[2]) : found->second;
      const double position=number<double>(argv[3])*65536;
      if(!std::isfinite(position) || position<-2147483648.0 || position>2147483647.0)throw std::runtime_error("beat outside 16.16 range");
      const auto beat=static_cast<std::int32_t>(position);auto env=wz3::frame_environment(beat);
      std::unique_ptr<wz3::EventAnimationScope> event;
      if(argc==5)event=std::make_unique<wz3::EventAnimationScope>(project.events.at(number<std::size_t>(argv[4])),beat,splines,env);
      const auto& op=project.operators.at(id);const auto program=wz3::compile_animation(op,splines);
      const wz3::AnimationScope result(program,splines,op.parameters,env);
      std::cout<<"{\"operator\":"<<id<<",\"beat_16_16\":"<<beat<<",\"time\":"<<env.variables[0][0];
      if(argc==5) {
        const auto index=number<std::size_t>(argv[4]);
        std::cout<<",\"event\":"<<index<<",\"event_active\":"<<(wz3::event_active(project.events[index],beat) ? "true" : "false");
      }
      std::cout<<",\"source_nodes\":"<<program.source_nodes<<",\"used_nodes\":"<<program.used_nodes<<",\"parameter_bits\":[";
      for(std::size_t i=0;i<result.parameters.size();++i)std::cout<<(i ? "," : "")<<result.parameters[i];
      std::cout<<"],\"as_floats\":[";
      for(std::size_t i=0;i<result.parameters.size();++i) {std::cout<<(i ? "," : "");real(wz3::as_float(result.parameters[i]));}
      std::cout<<"],\"variables\":[";
      for(unsigned i=0;i<32;++i) {
        std::cout<<(i ? ",[" : "[");for(unsigned c=0;c<4;++c) {std::cout<<(c ? "," : "");real(env.variables[i][c]);}std::cout<<"]";
      }
      std::cout<<"],\"scope\":\"operator parameters only; scene traversal and rendering are not performed\"}\n";
    }
  }catch(const std::exception& error) {std::cerr<<"animation: "<<error.what()<<'\n';return 1;}
}
