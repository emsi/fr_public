// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "wz3/audio.hpp"
#include <SDL.h>
namespace wz3 {
class SdlAudio {
  SDL_AudioDeviceID device{};
  AudioPlayback playback;
  SDL_AudioSpec spec{};
  static void callback(void*,Uint8*,int) noexcept;
public:
  SdlAudio(const PcmAudio&,bool mute=false);
  ~SdlAudio();
  SdlAudio(const SdlAudio&)=delete;
  SdlAudio& operator=(const SdlAudio&)=delete;
  void start();
  std::uint64_t position();
  std::uint64_t callback_count();
  unsigned buffer_frames() const {return spec.samples;}
};
}
