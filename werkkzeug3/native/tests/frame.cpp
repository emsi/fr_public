// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "frame_fixture.hpp"
#include <iostream>
#include <sstream>
namespace {
template<class F> void rejects(F fn,const char* reason) {
  bool caught=false;try {fn();}catch(const std::runtime_error& e) {caught=std::string_view(e.what()).find(reason)!=std::string_view::npos;}
  check(caught,"frame did not reject the expected unsupported operation");
}
void order_and_scopes() {
  FrameFixture x;const auto a=x.material(0xff800000),b=x.material(0xff000080);x.animate(a,15,24);x.animate(b,15,24);
  const auto one=x.layer(a),two=x.layer(b);const auto branch=x.add(3,{}, {one});x.animate(branch,0,15,0x8f);
  const auto root=x.add(0x0d,{}, {branch,two});wz3::FramePlayer player(x.p,x.g);const auto frame=player.frame(root,16384,64,48);
  check(frame.commands.size()==4,"Demo must copy each IPP input in order");
  check(frame.commands[0].operator_id==one && frame.commands[1].target.surface==0 && frame.commands[2].operator_id==two && frame.commands[3].target.surface==0,"Demo draw order");
  check(frame.commands[0].target.surface==frame.commands[2].target.surface,"Demo must reset the target pool between inputs");
  const auto& qa=std::get<wz3::FrameQuad>(frame.commands[0].draw);const auto& qb=std::get<wz3::FrameQuad>(frame.commands[2].draw);
  near(wz3::as_float(qa.shader.words[24]),.25f,"ancestor global animation reaches material execution");
  near(wz3::as_float(qb.shader.words[24]),0,"ancestor global animation must be restored before siblings");
  const auto& copy=std::get<wz3::FrameQuad>(frame.commands[1].draw);near(copy.uv[0][2],64/1024.0f,"cropped full-size UV width");near(copy.uv[0][3],48/512.0f,"cropped full-size UV height");
  check(frame.commands[0].target.width==64 && frame.commands[0].target.height==48,"full target viewport crop");
  player.frame(root,32768,64,48);near(wz3::as_float(qa.shader.words[24]),.25f,"previous frame shader snapshot changed");
}
void fanout_and_blur() {
  FrameFixture x;const auto layer=x.layer(x.material(0xff202020));const auto select=x.add(0xea,{}, {layer});
  const auto color=x.add(0xe6,{0,0xff202020,0,0x404040},{select});
  const auto blur=x.add(0xe3,{5,f(2),f(1),1,2},{color});
  const auto merge=x.add(0xe7,{3,1,128,0x404040},{select,blur});const auto root=x.add(0x0d,{}, {merge});
  wz3::FramePlayer player(x.p,x.g);const auto frame=player.frame(root,0,64,48);
  check(frame.cache_hits==1 && frame.screen_fallbacks==0,"one shared input must be reused without leaking targets");
  check(frame.commands.size()==8,"two-stage blur must emit two draws per stage");
  check(frame.commands[0].operator_id==layer && frame.commands[1].operator_id==color,"Color must execute after shared source");
  const auto& q=std::get<wz3::FrameQuad>(frame.commands[2].draw);
  near(q.uv[0][1],-.25f/512,"diamond blur vertical offset includes half radius");
  check(q.shader.words[28]==0x40000000 && !(q.shader.words[0]&0xf000),"first blur pair overwrites at quantized weight");
  check((std::get<wz3::FrameQuad>(frame.commands[3].draw).shader.words[0]&0xf000)==0x2000,"second blur pair adds");
  for(const auto& c:frame.commands)if(const auto* q=std::get_if<wz3::FrameQuad>(&c.draw))for(const auto t:q->textures)
    check(!t || !t->surface || t->id!=c.target.surface,"pool reused a texture while it was being sampled");
}
void nested_targets_and_capacity() {
  FrameFixture x;const auto material=x.material(0xff204060),layer=x.layer(material);
  const auto inner=x.add(0x26,{6,5,1}),outer=x.add(0x26,{7,6,1});
  const auto first=x.add(0xeb,{}, {layer},{inner}),second=x.add(0xeb,{}, {first},{outer});
  const auto after=x.layer(material,4);const auto root=x.add(0x0d,{}, {second,after});
  wz3::FramePlayer player(x.p,x.g);const auto frame=player.frame(root,0,80,60);
  check(frame.commands.size()==5,"nested RenderTarget copy count");
  check(frame.commands[0].target.width==64 && frame.commands[0].target.height==32,"nested master changes full-size crop");
  check(frame.commands[1].target.surface==13+inner && frame.commands[2].target.surface==13+outer,"nested target copy order");
  check(frame.commands[4].target.surface==0 && frame.commands[4].target.width==80 && frame.commands[4].target.height==60,"master target restored after nested calls");
  near(std::get<wz3::FrameQuad>(frame.commands[2].draw).uv[0][2],128/1024.0f,"restored master recomputes pool UV scales");

  FrameFixture full;std::vector<std::size_t> layers;const auto mat=full.material(0xff000000);
  for(unsigned i=0;i<4;++i)layers.push_back(full.layer(mat,0));const auto select=full.add(0xea,{},layers);const auto demo=full.add(0x0d,{}, {select});
  wz3::FramePlayer crowded(full.p,full.g);const auto fallback=crowded.frame(demo,0,64,48);
  check(fallback.screen_fallbacks==1 && fallback.commands.size()==4 && fallback.commands.back().target.surface==0,"three pool slots per size then screen fallback");
}
void events_and_failures() {
  FrameFixture x;const auto material=x.material(0xffabcdef);x.animate(material,0,24);
  const auto layer=x.layer(material,4),event=x.event(layer,"active",65536,3*65536);const auto root=x.add(0x0d,{}, {event});
  const auto unsupported=x.add(0xec),bad=x.add(0x0d,{}, {unsupported});
  wz3::FramePlayer player(x.p,x.g);check(player.frame(root,0,64,48).commands.empty(),"inactive event drew a layer");
  const auto frame=player.frame(root,2*65536,64,48);check(frame.commands.size()==1,"active event should draw directly to screen");
  near(wz3::as_float(std::get<wz3::FrameQuad>(frame.commands[0].draw).shader.words[24]),.5f,"normalized event time reaches material");
  check(player.frame(root,3*65536,64,48).commands.empty(),"event end is exclusive");
  rejects([&]{player.frame(bad,0,64,48);},"not ported");check(player.frame(root,2*65536,64,48).commands.size()==1,"failed traversal poisoned player");
  FrameFixture zero;const auto instant=zero.event(zero.layer(zero.material(0xff000000),4),"zero",0,0,2);const auto demo=zero.add(0x0d,{}, {instant});
  wz3::FramePlayer starts(zero.p,zero.g);check(starts.frame(demo,5*65536,64,48).commands.size()==1,"zero-length event starts at dispatch");
  check(starts.frame(demo,6*65536,64,48).commands.size()==1 && starts.frame(demo,7*65536,64,48).commands.empty(),"event runtime duration must persist between frames");
}
void shared_assets() {
  FrameFixture x;const auto viewport=x.viewport(),event=x.event(viewport,"zero",0,0,1),root=x.add(0x0d,{}, {event});
  const auto assets=std::make_shared<wz3::SceneAssets>(x.p,x.g);
  std::shared_ptr<const wz3::Mesh> loaded_mesh;
  {
    wz3::FramePlayer loading(x.p,x.g,{},assets);
    const auto loaded=loading.frame(root,5*65536,64,48,2500);
    loaded_mesh=std::get<wz3::SceneFrame>(loaded.commands[0].draw).instances[0].mesh;
    check(loading.frame(root,6*65536,64,48,3000).commands.empty(),"loading must reach an expired dynamic event");
  }
  wz3::FramePlayer fresh(x.p,x.g,{},assets),cold(x.p,x.g);
  const auto a=fresh.frame(root,0,64,48,0),b=cold.frame(root,0,64,48,0);
  check(!a.commands.empty() && a.commands.size()==b.commands.size(),"loading leaked dynamic event times into playback");
  check(std::get<wz3::SceneFrame>(a.commands[0].draw).instances[0].mesh==loaded_mesh,"fresh playback regenerated cached geometry");
  std::ostringstream warm_json,cold_json;wz3::write_frame(warm_json,a);wz3::write_frame(cold_json,b);
  check(warm_json.str()==cold_json.str(),"shared assets changed initial frame execution");
  check(assets->stats().meshes==1 && assets->stats().mesh_bytes>0,"asset cache accounting");
  wz3::Project other=x.p;
  rejects([&]{wz3::FramePlayer wrong(other,x.g,{},assets);},"different project");
  rejects([&]{wz3::FramePlayer wrong(x.p,x.g,{"another-font.ttf"},assets);},"different project");
}
void theta(const char* path) {
  const auto p=wz3::read_project(path);const auto g=wz3::connect(p);wz3::FramePlayer player(p,g);const auto root=g.names.at("root");
  for(auto beat:{56,72,144,216,384}) {
    const auto frame=player.frame(root,beat*65536,1024,768);unsigned scenes=0;
    for(const auto& c:frame.commands)if(std::holds_alternative<wz3::SceneFrame>(c.draw))++scenes;
    check(frame.screen_fallbacks==0,"Theta root exhausted its target pool");check(frame.commands.size()==(beat==384 ? 17u : 14u),"Theta root draw count changed");
    check(scenes==(beat==384 ? 2u : 1u),"Theta viewport count changed");
    check(frame.commands[frame.commands.size()-2].operator_id==528 && frame.commands.back().operator_id==527,"Theta bars must be drawn last");
    std::ostringstream out;wz3::write_frame(out,frame);check(out.str().find("\"commands\":[")!=std::string::npos,"frame JSON export");
    std::cout<<"Theta beat "<<beat<<": "<<frame.commands.size()<<" commands, "<<scenes<<" viewports\n";
  }
  for(const auto sample:std::array<std::array<unsigned,3>,4>{{{96,697,333},{120,1622,185},{184,827,645},{256,924,754}}}) {
    const auto frame=player.frame(root,sample[0]*65536,1024,768);
    check(frame.commands.size()==14 && frame.screen_fallbacks==0,"Theta particle scene compositor command count");
    const auto& scene=std::get<wz3::SceneFrame>(frame.commands[0].draw);
    check(scene.viewport_id==sample[1] && scene.instances.size()==sample[2],"Theta particle scene instance count");
  }
  const auto opening=player.frame(root,0,1024,768);
  check(!std::get<wz3::SceneFrame>(opening.commands[0].draw).effects.empty(),"opening particle effect was not queued");
}
}
int main(int argc,char** argv) {
  try {order_and_scopes();fanout_and_blur();nested_targets_and_capacity();events_and_failures();shared_assets();if(argc>1)theta(argv[1]);}
  catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
