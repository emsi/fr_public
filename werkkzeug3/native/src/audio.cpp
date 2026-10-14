// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/audio.hpp"
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
PlaybackTiming::PlaybackTiming(const Project& project,unsigned sample_rate):rate(sample_rate) {
  const double bpm=as_float(project.settings[4]);
  require(std::isfinite(bpm) && bpm>=1 && bpm<=1000,"invalid playback BPM");
  require(rate>=8000 && rate<=192000,"invalid playback sample rate");
  require(project.settings[6]>0 && project.settings[6]<=std::uint32_t(std::numeric_limits<std::int32_t>::max()),"invalid saved demo endpoint");
  bpm_fixed=std::int32_t(bpm*65536);end_beat=std::int32_t(project.settings[6]);
  strict_endpoint=project.release_timing;
  if(strict_endpoint)require(bpm_fixed==138*65536 && end_beat==592*65536 && project.settings[9]==1,"invalid released clock profile");
  if(project.settings[9])buzz_tick=60*44100/((bpm_fixed>>16)*8);
}
std::int32_t PlaybackTiming::beat(std::uint64_t frames) const {
  // Also keeps all integer products below 2^64 for unusual caller input.
  require(frames<=std::uint64_t(rate)*24*60*60,"playback time exceeds 24 hours");
  const auto value=buzz_tick ? (frames*44100/rate)*65536/buzz_tick/8 : frames*std::uint64_t(bpm_fixed)/(std::uint64_t(rate)*60);
  require(value<=std::uint64_t(std::numeric_limits<std::int32_t>::max()),"playback beat exceeds 16.16 range");return std::int32_t(value);
}
std::int32_t PlaybackTiming::milliseconds(std::uint64_t frames) const {
  require(frames<=std::uint64_t(rate)*24*60*60,"playback time exceeds 24 hours");return std::int32_t(frames*1000/rate);
}
std::uint64_t PlaybackTiming::end_frames() const {
  // The recovered release exits on beat > 592. Editor/provisional projects
  // retain their existing first-sample-at-or-after endpoint convention.
  const auto threshold=std::uint64_t(end_beat)+(strict_endpoint ? 1 : 0);
  if(buzz_tick) {
    const auto samples44100=(threshold*buzz_tick*8+65535)/65536;
    return (samples44100*rate+44099)/44100;
  }
  return (threshold*rate*60+bpm_fixed-1)/bpm_fixed;
}
} // namespace wz3
