// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "audio_sdl.hpp"
#include <chrono>
#include <stdexcept>
namespace wz3 {
namespace {
std::uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
SdlAudio::SdlAudio(const PcmAudio& pcm,bool mute):playback(pcm,mute) {
  SDL_AudioSpec requested{};requested.freq=pcm.rate;requested.channels=pcm.channels;
  requested.format=AUDIO_F32SYS;requested.samples=1024;requested.callback=callback;requested.userdata=this;
  device=SDL_OpenAudioDevice(nullptr,0,&requested,&spec,0);
  if(!device)throw std::runtime_error(std::string("open audio device: ")+SDL_GetError());
}
SdlAudio::~SdlAudio(){if(device)SDL_CloseAudioDevice(device);}
void SdlAudio::callback(void* userdata,Uint8* stream,int bytes) noexcept {
  auto& self=*static_cast<SdlAudio*>(userdata);
  self.playback.fill({reinterpret_cast<float*>(stream),std::size_t(bytes)/sizeof(float)},now_ns());
}
void SdlAudio::start(){SDL_PauseAudioDevice(device,0);}
std::uint64_t SdlAudio::position() {
  SDL_LockAudioDevice(device);const auto frames=playback.position(now_ns());SDL_UnlockAudioDevice(device);return frames;
}
std::uint64_t SdlAudio::callback_count() {
  SDL_LockAudioDevice(device);const auto count=playback.callback_count();SDL_UnlockAudioDevice(device);return count;
}
}
