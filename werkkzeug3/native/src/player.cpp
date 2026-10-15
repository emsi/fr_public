// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/frame.hpp"
#include "wz3/frame_preload.hpp"
#include "wz3/texture.hpp"
#include "wz3/playback_timing.hpp"
#include "audio_sdl.hpp"
#include <GLES3/gl3.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace {
using Clock=std::chrono::steady_clock;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class T> T number(std::string_view text) {
  T value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  require(error==std::errc{} && end==text.data()+text.size(),"invalid numeric option");return value;
}
void sdl_check(bool ok,const char* action) {
  if(!ok)throw std::runtime_error(std::string(action)+": "+SDL_GetError());
}
struct Sdl {
  explicit Sdl(bool audio){if(SDL_Init(SDL_INIT_VIDEO|(audio ? SDL_INIT_AUDIO : 0))!=0){const std::string error=SDL_GetError();SDL_Quit();throw std::runtime_error("SDL initialization: "+error);}}
  ~Sdl(){SDL_Quit();}
};
struct Window {
  SDL_Window* window{};SDL_GLContext context{};
  Window(unsigned width,unsigned height) {
    try {
      SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL,"1");
      sdl_check(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES)==0,"GLES profile");
      sdl_check(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3)==0,"GLES version");
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
      SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);
      SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
      SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
      window=SDL_CreateWindow("fr-038: Theta - loading",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width,height,
        SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
      sdl_check(window,"create window");context=SDL_GL_CreateContext(window);sdl_check(context,"create GLES3 context");
      if(SDL_GL_SetSwapInterval(1)!=0)std::cerr<<"Vsync unavailable; pacing presentation to 60 Hz\n";
    }catch(...){release();throw;}
  }
  void release(){if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);}
  ~Window(){release();}
  bool events() {
    SDL_Event event;while(SDL_PollEvent(&event)) {
      if(event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE))return false;
    }
    return true;
  }
  void present(wz3::FrameRenderer& renderer) {
    int width=0,height=0;SDL_GL_GetDrawableSize(window,&width,&height);
    if(width>0 && height>0){renderer.present(width,height);SDL_GL_SwapWindow(window);}
  }
  void loading(double progress) {
    const auto title="fr-038: Theta - preparing "+std::to_string(int(progress*100))+"% (Escape to cancel)";
    SDL_SetWindowTitle(window,title.c_str());
    int width=0,height=0;SDL_GL_GetDrawableSize(window,&width,&height);if(width<=0 || height<=0)return;
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(.025f,.03f,.04f,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(width/8,height/2,width*3/4,std::max(4,height/64));
    glClearColor(.12f,.15f,.19f,1);glClear(GL_COLOR_BUFFER_BIT);
    glScissor(width/8,height/2,int(width*3/4*progress),std::max(4,height/64));
    glClearColor(.65f,.8f,1,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    SDL_GL_SwapWindow(window);
  }
};
struct Snapshot {double seconds{};std::filesystem::path path;bool saved=false;};
}
int main(int argc,char** argv) {
  if(argc<2 || std::string_view(argv[1])=="--help") {
    std::cout<<"Usage: "<<argv[0]<<" PROJECT.k|theta.wz3r [--font-file FONT.ttf] [--music SONG.ogg|SONG.v2m]\n"
      <<"  [--width PIXELS] [--height PIXELS] [--duration SECONDS] [--mute]\n"
      <<"  [--capture LAST.ppm] [--snapshot SECONDS FRAME.ppm] (repeatable)\n"
      <<"  [--no-preload | --preload-only | --offline]\n"
      <<"  [--preload-full] (render every preparation frame for comparison)\n"
      <<"Offline mode renders the same sample timeline without an audio device or real-time pacing.\n"
      <<"Escape or close the window to exit. Music defaults to the saved song's basename beside the project.\n";
    return argc<2 ? 2 : 0;
  }
  try {
    wz3::TextureOptions options;std::filesystem::path music,capture;unsigned width=0,height=0;
    double duration=0;bool mute=false,preload=true,preload_only=false,offline=false,preload_full=false;std::vector<Snapshot> snapshots;
    for(int i=2;i<argc;++i) {
      const std::string_view option=argv[i];
      if(option=="--mute")mute=true;
      else if(option=="--no-preload")preload=false;
      else if(option=="--preload-only")preload_only=true;
      else if(option=="--preload-full")preload_full=true;
      else if(option=="--offline")offline=true;
      else if(option=="--music" && i+1<argc)music=argv[++i];
      else if(option=="--font-file" && i+1<argc)options.font_file=argv[++i];
      else if(option=="--capture" && i+1<argc)capture=argv[++i];
      else if(option=="--width" && i+1<argc)width=number<unsigned>(argv[++i]);
      else if(option=="--height" && i+1<argc)height=number<unsigned>(argv[++i]);
      else if(option=="--duration" && i+1<argc) {
        duration=number<double>(argv[++i]);require(std::isfinite(duration) && duration>0 && duration<=1800,"duration must be in (0,1800] seconds");
      }else if(option=="--snapshot" && i+2<argc) {
        const auto seconds=number<double>(argv[++i]);require(std::isfinite(seconds) && seconds>=0 && seconds<=1800,"snapshot must be in [0,1800] seconds");
        snapshots.push_back({seconds,argv[++i],false});
      }else throw std::runtime_error("unknown or incomplete player option: "+std::string(option));
    }
    require(options.font_file.empty() || std::filesystem::is_regular_file(options.font_file),"--font-file does not exist");
    require(!offline || !preload_only,"--offline conflicts with --preload-only");
    if(offline)preload=false;
    require(preload || !preload_only,"--no-preload conflicts with --preload-only");
    require(preload || !preload_full,"--preload-full requires preloading");
    const std::filesystem::path project_path=argv[1];const auto project=wz3::read_project(project_path);const auto graph=wz3::connect(project);
    if(project.runtime_graph)std::cerr<<(project.release_timing ? "Released clock: 2396 samples per eighth beat; exit after beat 592\n" : "Released graph: using explicit provisional timing from the native envelope\n");
    const auto root=graph.names.find("root");require(root!=graph.names.end(),"project has no named root");
    if(!width)width=project.settings[2];
    if(!height)height=project.settings[3];
    require(width>=64 && height>=64 && width<=4096 && height<=4096,"render dimensions must be 64..4096");
    if(music.empty()) {
      auto name=project.song_name;std::replace(name.begin(),name.end(),'\\','/');
      require(!name.empty(),"project has no music path; supply --music");
      music=project_path.parent_path()/std::filesystem::path(name).filename();
    }
    auto extension=music.extension().string();
    for(auto& c:extension)if(c>='A' && c<='Z')c=char(c-'A'+'a');
    const bool v2=extension==".v2m";
    std::cout<<std::unitbuf<<(v2 ? "Synthesizing " : "Decoding ")<<music<<'\n';
    const auto pcm=[&] {
      if(!v2)return wz3::read_vorbis(music);
      auto frames=wz3::PlaybackTiming(project,44100).end_frames();
      if(duration)frames=std::min(frames,std::uint64_t(std::ceil(duration*44100)));
      return wz3::read_v2m_audio(music,frames);
    }();
    wz3::PlaybackTiming timing(project,pcm.rate);auto end=timing.end_frames();
    if(duration)end=std::min(end,std::uint64_t(std::ceil(duration*pcm.rate)));
    std::cout<<"Music: "<<pcm.rate<<" Hz, "<<pcm.channels<<" channels, "<<double(pcm.frames())/pcm.rate<<" seconds\n";
    Sdl sdl(!offline && !preload_only);std::unique_ptr<wz3::SdlAudio> audio;
    if(!preload_only && !offline)audio=std::make_unique<wz3::SdlAudio>(pcm,mute);
    Window window(width,height);options=wz3::rendered_texture_options(project,graph,options);
    options.cache=std::make_shared<wz3::TextureCache>(project,graph,options);
    auto assets=std::make_shared<wz3::SceneAssets>(project,graph,options);
    wz3::FrameRenderer renderer(project,graph,options,wz3::FrameRenderer::Context::Current);
    std::cout<<"Video: "<<SDL_GetCurrentVideoDriver()<<"; audio: "<<(SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none")
      <<"; callback: "<<(audio ? audio->buffer_frames() : 0)<<" frames; render: "<<width<<'x'<<height<<'\n';
    if(preload) {
      const auto started=Clock::now();auto progress=started;std::uint64_t frames=0,draws=0,last_log=0;
      window.loading(0);
      {
        wz3::FramePlayer loading(project,graph,options,assets);
        wz3::FramePreloader preparation;
        for(std::uint64_t ms=0;;ms+=16) {
          if(!window.events()){std::cout<<"Loading cancelled\n";return 0;}
          const auto sample=std::min(ms*pcm.rate/1000,end);
          try{
            const auto frame=loading.frame(root->second,timing.beat(sample),width,height,timing.milliseconds(sample));
            if(preload_full || preparation.needs_draw(frame)) {
              renderer.draw(frame);++draws;
              // Prime both ping-pong images even for a one-frame effect.
              if(!preload_full){renderer.draw(frame);++draws;}
            }
          }
          catch(const std::exception& e){throw std::runtime_error("loading at "+std::to_string(timing.milliseconds(sample))+" ms: "+e.what());}
          ++frames;const auto now=Clock::now();
          if(now-progress>=std::chrono::milliseconds(100)){window.loading(double(sample)/end);progress=now;}
          if(sample*10/end!=last_log){last_log=sample*10/end;std::cout<<"Preparing "<<last_log*10<<"%\n";}
          if(sample==end)break;
        }
      } // Discard simulation, animation and event state before starting music.
      renderer.reset_targets();window.loading(1);
      const auto cpu=assets->stats();const auto gpu=renderer.stats();
      std::cout<<"Loading complete: "<<frames<<" frames in "<<std::chrono::duration<double>(Clock::now()-started).count()
        <<" seconds; "<<cpu.meshes<<" meshes, "<<cpu.fonts<<" font atlases, "<<gpu.bitmap_uploads
        <<" bitmap uploads, "<<gpu.program_compilations<<" shader programs\n";
      std::cout<<"Preparation draws: "<<draws<<" ("<<(preload_full ? "full timeline" : "new resource configurations")<<")\n";
      std::cout<<"Bitmap cache: "<<gpu.bitmap_bytes<<" bytes of uploaded mip data\n";
      const auto textures=options.cache->stats();
      std::cout<<"Shared texture recipes: "<<textures.hits<<" reused branches, "<<textures.pixel_bytes<<" bytes retained, "<<textures.evictions<<" evictions\n";
      if(preload_only)return 0;
    }
    const auto prepared_cpu=assets->stats();const auto prepared_gpu=renderer.stats();
    wz3::FramePlayer player(project,graph,options,assets);
    std::uint64_t rendered=0,presented=0,current=0,next_ms=16;double max_draw_ms=0,max_lag_ms=0;
    auto draw=[&](std::uint64_t sample) {
      const auto start=Clock::now();
      try{renderer.draw(player.frame(root->second,timing.beat(sample),width,height,timing.milliseconds(sample)));}
      catch(const std::exception& e){throw std::runtime_error("at "+std::to_string(timing.milliseconds(sample))+" ms: "+e.what());}
      max_draw_ms=std::max(max_draw_ms,std::chrono::duration<double,std::milli>(Clock::now()-start).count());
      current=sample;++rendered;
      for(std::size_t i=0;i<snapshots.size();++i)if(auto& snapshot=snapshots[i]; !snapshot.saved && double(sample)/pcm.rate>=snapshot.seconds) {
        wz3::write_ppm(snapshot.path,width,height,renderer.readback());snapshot.saved=true;
        std::ostringstream record;record<<std::setprecision(17)
          <<"Snapshot: {\"version\":1,\"index\":"<<i<<",\"requested_seconds\":"<<snapshot.seconds
          <<",\"sample\":"<<sample<<",\"sample_rate\":"<<pcm.rate<<",\"frame_index\":"<<rendered-1
          <<",\"milliseconds\":"<<timing.milliseconds(sample)<<",\"beat_fixed\":"<<timing.beat(sample)<<"}\n";
        std::cout<<record.str();
      }
    };
    draw(0);window.present(renderer);SDL_SetWindowTitle(window.window,"fr-038: Theta - Escape to exit");
    // No sound advances during loading. Subsequent fixed 16 ms steps
    // retain simulation/target history, including while catching up after a stall.
    if(audio) {require(audio->position()==0 && audio->callback_count()==0,"audio advanced during loading");audio->start();}
    const auto started=Clock::now();auto last_progress=started,next_presentation=started;std::uint64_t last_sample=0,last_log=0;
    bool running=true;while(running && current<end) {
      const auto tick=Clock::now();running=window.events();if(!running)break;
      const auto sample=offline ? std::min(next_ms*pcm.rate/1000,end) : audio->position(),target=std::min(sample,end);
      if(sample!=last_sample){last_sample=sample;last_progress=tick;}
      if(!offline)require(tick-last_progress<std::chrono::seconds(2),"audio clock stalled for two seconds");
      max_lag_ms=std::max(max_lag_ms,double(target>current ? target-current : 0)*1000/pcm.rate);
      // Bound each batch so events remain responsive on a slow renderer.
      for(unsigned batch=0;batch<32 && current<target;++batch) {
        const auto next=std::min(next_ms*pcm.rate/1000,end);if(next>target)break;
        draw(next);next_ms+=16;
      }
      if(!offline || tick>=next_presentation) {window.present(renderer);++presented;next_presentation=tick+std::chrono::milliseconds(100); }
      if(sample/pcm.rate/10!=last_log) {
        last_log=sample/pcm.rate/10;
        std::cout<<(offline ? "Offline " : "Playback ")<<timing.milliseconds(current)<<" ms, beat "<<double(timing.beat(current))/65536
          <<", "<<rendered<<" rendered frames\n";
      }
      // Prevent an unbounded busy loop when the window system ignores vsync.
      if(!offline)std::this_thread::sleep_until(tick+std::chrono::microseconds(16667));
    }
    if(!capture.empty())wz3::write_ppm(capture,width,height,renderer.readback());
    std::cout<<(offline ? "Offline " : "Playback ")<<(running ? "complete" : "stopped")<<": "<<rendered<<" rendered frames, "<<presented
      <<" presentations, "<<(audio ? audio->callback_count() : 0)<<" audio callbacks, "<<timing.milliseconds(current)<<" ms, beat "
      <<double(timing.beat(current))/65536<<", "<<std::chrono::duration<double>(Clock::now()-started).count()
      <<" wall seconds; max draw "<<max_draw_ms<<" ms; max catch-up "<<max_lag_ms<<" ms\n";
    const auto cpu=assets->stats();const auto gpu=renderer.stats();
    std::cout<<"Bitmap cache: "<<gpu.bitmap_bytes<<" bytes of uploaded mip data\n";
    std::cout<<(offline ? "Offline allocations: " : "Playback allocations: ")<<cpu.meshes-prepared_cpu.meshes<<" meshes, "<<cpu.fonts-prepared_cpu.fonts
      <<" fonts, "<<gpu.surface_allocations-prepared_gpu.surface_allocations<<" surfaces, "<<gpu.bitmap_uploads-prepared_gpu.bitmap_uploads
      <<" bitmaps, "<<gpu.mesh_uploads-prepared_gpu.mesh_uploads<<" mesh uploads, "<<gpu.shadow_uploads-prepared_gpu.shadow_uploads
      <<" shadow uploads, "<<gpu.instance_uploads-prepared_gpu.instance_uploads<<" instance buffers, "<<gpu.bitmap_renders-prepared_gpu.bitmap_renders<<" bitmap renders, "<<gpu.program_compilations-prepared_gpu.program_compilations<<" programs, "<<gpu.lighting_uploads-prepared_gpu.lighting_uploads<<" lighting tables\n";
  }catch(const std::exception& e){std::cerr<<"player: "<<e.what()<<'\n';return 1;}
}
