#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ptvaudio {
// Native AvPlayer supplies interleaved signed 16-bit PCM in the supported
// audio path. No codec/container or device names are required.
struct ResampleState {
  uint32_t sample_rate=0;
  uint32_t channels=0;
  uint64_t remainder=0;
};
inline int16_t clamp16(int value) {
  return static_cast<int16_t>(std::max(-32768,std::min(32767,value)));
}
inline int16_t read16(const uint8_t* data) {
  const uint16_t sample=uint16_t(data[0])|(uint16_t(data[1])<<8);
  return static_cast<int16_t>(sample);
}
inline bool append_pcm48_stereo(const uint8_t* data,size_t bytes,
                               uint32_t channels,uint32_t sample_rate,
                               ResampleState& state,
                               std::vector<int16_t>& queue,
                               size_t maximum_frames=8192) {
  if(!data||channels<1||channels>8||
     sample_rate<8000||sample_rate>192000||
     queue.size()%2 || maximum_frames>65536 ||
     bytes<(size_t)channels*2)return false;
  const size_t bytes_per_frame=size_t(channels)*2;
  const size_t input_frames=bytes/bytes_per_frame;
  // A corrupt size field must not make an unbounded allocation.
  if(input_frames>65536)return false;
  if(state.sample_rate!=sample_rate||state.channels!=channels) {
    state.sample_rate=sample_rate;
    state.channels=channels;
    state.remainder=0;
  }
  const uint64_t scaled=uint64_t(input_frames)*48000ULL+state.remainder;
  const size_t output_frames=size_t(scaled/sample_rate);
  if(output_frames>maximum_frames||
     queue.size()/2>maximum_frames-output_frames)return false;
  // For a 48k stream this is an exact copy of the original stereo samples.
  const size_t previous=queue.size();
  queue.resize(previous+output_frames*2);
  for(size_t i=0;i<output_frames;i++) {
    // Linear interpolation for 44.1k, 32k etc. Keep the output at 48k.
    const uint64_t position=uint64_t(i)*sample_rate;
    const size_t a=std::min(input_frames-1,size_t(position/48000));
    const size_t b=std::min(input_frames-1,a+1);
    const unsigned fraction=unsigned(position%48000);
    for(unsigned channel=0;channel<2;channel++) {
      // Mono duplicates into both channels; multichannel uses the decoded
      // front-left/front-right pair, never guesses a surround channel map.
      const unsigned source_channel=channels==1?0:channel;
      const int va=read16(data+a*bytes_per_frame+source_channel*2);
      const int vb=read16(data+b*bytes_per_frame+source_channel*2);
      const int interpolated=int((int64_t(va)*(48000-fraction)+
                                int64_t(vb)*fraction+24000)/48000);
      queue[previous+i*2+channel]=clamp16(interpolated);
    }
  }
  state.remainder=scaled%sample_rate;
  return true;
}
}
