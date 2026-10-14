// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/frame.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
inline void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
inline void near(float a,float b,const char* message) {check(std::abs(a-b)<.00001f,message);}
inline std::uint32_t f(float v) {return std::bit_cast<std::uint32_t>(v);}
struct FrameFixture {
  wz3::Project p;wz3::Graph g;
  FrameFixture() {p.settings[4]=f(120); }
  std::size_t add(unsigned command,std::vector<std::uint32_t> parameters={},std::vector<std::size_t> inputs={},std::vector<std::optional<std::size_t>> links={}) {
    wz3::Operator op;op.command=command;op.parameters=std::move(parameters);p.operators.push_back(std::move(op));g.nodes.push_back({true,true,std::move(inputs),std::move(links)});return p.operators.size()-1;
  }
  std::size_t material(std::uint32_t color,std::uint32_t flags=0) {
    std::vector<std::uint32_t> p(64);p[0]=flags|0x20002;p[9]=0x10;p[28]=color;p[48]=1;
    for(unsigned i=0;i<4;++i)p[24+i]=f(1);
    p[34]=p[35]=p[36]=p[43]=p[44]=f(1);return add(0xd0,p);
  }
  std::size_t layer(std::size_t material,unsigned size=3,wz3::FrameRect xy={0,0,1,1}) {
    return add(0xe9,{size,f(xy[0]),f(xy[1]),f(xy[2]),f(xy[3]),0,0,f(1),f(1),0,0},{},{material});
  }
  std::size_t viewport(unsigned size=3) {
    const auto cube=add(0x81,{1,1,1,1,f(1),f(1),f(1),0,0,0,0,0,0});
    return add(0xf0,{size,3,0xff000000,0,0,0,0,0,f(-5),f(4096),f(.125f),0,0,f(1),f(1),0xff000000,f(4096),f(16)},{cube});
  }
  void animate(std::size_t id,unsigned variable,unsigned offset,unsigned store=0x91) {
    wz3::Animation load,write;load.fields={0,0,3,2,variable,0,0,0};write.fields={0,1,3,store,offset,5,0,0};p.operators[id].animations={load,write};
  }
  std::size_t event(std::size_t input,const std::string& name,unsigned begin,unsigned end,float duration=0) {
    const auto id=add(6,{f(duration)},{input});g.names.emplace(name,id);wz3::Event e;e.name=name;e.fields[1]=begin;e.fields[2]=end;
    e.fields[4]=e.fields[5]=f(1);e.fields[9]=0xffffffff;e.interval={0,f(1)};e.transform={f(1),f(1),f(1),0,0,0,0,0,0};p.events.push_back(e);return id;
  }
};
