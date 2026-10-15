// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/audio.hpp"
#include "wz3/v2_synth.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vorbis/vorbisfile.h>
namespace wz3 {
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
constexpr std::size_t max_samples=256u*1024*1024/sizeof(float);
struct Vorbis {
  OggVorbis_File file{};
  explicit Vorbis(const std::filesystem::path& path) {
    if(ov_fopen(path.c_str(),&file)<0)throw std::runtime_error("cannot open Ogg Vorbis music: "+path.string());
  }
  ~Vorbis(){ov_clear(&file);}
};
}
PcmAudio read_vorbis(const std::filesystem::path& path) {
  Vorbis stream(path);PcmAudio pcm;const auto* info=ov_info(&stream.file,0);
  require(info && info->rate>=8000 && info->rate<=192000 && info->channels>=1 && info->channels<=2,"music must be mono/stereo Vorbis at 8..192 kHz");
  pcm.rate=info->rate;pcm.channels=info->channels;
  for(long i=1;i<ov_streams(&stream.file);++i) {
    const auto* next=ov_info(&stream.file,i);
    require(next && next->rate==info->rate && next->channels==info->channels,"changing music format in chained Vorbis is unsupported");
  }
  const auto count=ov_pcm_total(&stream.file,-1);
  require(count>0 && std::uint64_t(count)<=max_samples/pcm.channels,"music exceeds the 256 MiB decoded PCM budget or has no samples");
  pcm.samples.reserve(std::size_t(count)*pcm.channels);
  while(true) {
    float** channels=nullptr;int link=0;const auto frames=ov_read_float(&stream.file,&channels,4096,&link);
    require(frames>=0,"corrupt Vorbis packet in music");if(!frames)break;
    require(std::size_t(frames)<=(max_samples-pcm.samples.size())/pcm.channels,"decoded music exceeds the 256 MiB PCM budget");
    for(long frame=0;frame<frames;++frame)for(unsigned channel=0;channel<pcm.channels;++channel) {
      const float sample=channels[channel][frame];require(std::isfinite(sample),"non-finite music sample");
      pcm.samples.push_back(std::clamp(sample,-1.0f,1.0f));
    }
  }
  require(pcm.frames()==std::uint64_t(count),"decoded music length differs from Vorbis metadata");return pcm;
}
PcmAudio read_v2m_audio(const std::filesystem::path& path,std::uint64_t frames) {
  require(frames>0 && frames<=max_samples/2,"V2 music exceeds the 256 MiB PCM budget or has no samples");
  V2Synthesis synth(read_v2m(path));PcmAudio pcm{44100,2,{}};
  pcm.samples.resize(frames*2);synth.render(pcm.samples);return pcm;
}
AudioPlayback::AudioPlayback(const PcmAudio& audio,bool mute):pcm(audio),muted(mute) {
  require(pcm.rate>=8000 && pcm.rate<=192000 && pcm.channels>=1 && pcm.channels<=2 && pcm.samples.size()%pcm.channels==0,"invalid playback PCM format");
}
void AudioPlayback::fill(std::span<float> output,std::uint64_t now_ns) noexcept {
  const auto frames=output.size()/pcm.channels;
  anchor_frame=submitted;anchor_ns=now_ns;buffer_frames=frames;++callbacks;
  std::fill(output.begin(),output.end(),0);
  if(!muted && submitted<pcm.frames()) {
    const auto count=std::min<std::uint64_t>(frames,pcm.frames()-submitted)*pcm.channels;
    std::copy_n(pcm.samples.data()+submitted*pcm.channels,count,output.data());
  }
  // Keep delivering silence beyond EOF until the saved demo endpoint.
  submitted+=frames;
}
std::uint64_t AudioPlayback::position(std::uint64_t now_ns) const noexcept {
  if(!callbacks)return 0;
  const auto elapsed=now_ns>anchor_ns ? now_ns-anchor_ns : 0;
  const auto bounded=std::min(elapsed,buffer_frames*1000000000/pcm.rate+1);
  return anchor_frame+std::min(buffer_frames,bounded*pcm.rate/1000000000);
}
} // namespace wz3
