#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace ptvaudio {
// The output clock is continuous across AvPlayer PCM chunks. Its position is
// measured in units of 1/48000 input frames, so there is no floating-point
// drift or phase reset between calls. The previous input frame lets us
// interpolate across a chunk boundary without repeating an edge sample.
struct ResampleState {
  uint32_t sample_rate=0;
  uint32_t channels=0;
  uint64_t input_frames=0;
  uint64_t next_output_position=0;
  int16_t previous_left=0;
  int16_t previous_right=0;
  bool has_previous=false;
};

inline int16_t clamp16(int64_t value) {
  return static_cast<int16_t>(std::max<int64_t>(-32768,
                                      std::min<int64_t>(32767,value)));
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
  constexpr uint64_t kOutputRate=48000;
  if(!data||channels<1||channels>8||sample_rate<8000||
     sample_rate>192000||queue.size()%2||
     maximum_frames>65536||queue.size()/2>maximum_frames)
    return false;
  const size_t bytes_per_frame=size_t(channels)*sizeof(int16_t);
  if(bytes<bytes_per_frame||bytes%bytes_per_frame!=0)return false;
  const size_t count=bytes/bytes_per_frame;
  if(count>65536)return false;

  ResampleState next=state;
  if(next.sample_rate!=sample_rate||next.channels!=channels){
    next=ResampleState{};
    next.sample_rate=sample_rate;
    next.channels=channels;
  }
  if(next.input_frames>std::numeric_limits<uint64_t>::max()-count)
    return false;
  const uint64_t first=next.input_frames;
  const uint64_t end=first+count;
  uint64_t position=next.next_output_position;
  const size_t capacity=maximum_frames-queue.size()/2;
  size_t produced=0;
  while(position/kOutputRate<end){
    const uint64_t a=position/kOutputRate;
    const uint64_t fraction=position%kOutputRate;
    // Interpolation needs a second input sample, which may belong to the
    // next chunk. Do not manufacture it by clamping to the current edge.
    if(fraction!=0&&a==end-1)break;
    if(a<first&&(first==0||!next.has_previous||a!=first-1))
      return false;
    if(produced==capacity||
       position>std::numeric_limits<uint64_t>::max()-sample_rate)
      return false;
    ++produced;
    position+=sample_rate;
  }

  const size_t original=queue.size();
  try {
    queue.resize(original+produced*2);
  } catch(const std::bad_alloc&) {
    return false;
  } catch(const std::length_error&) {
    return false;
  }

  auto sample=[&](uint64_t index,unsigned channel)->int16_t {
    if(index<first)
      return channel==0?next.previous_left:next.previous_right;
    const size_t offset=size_t(index-first)*bytes_per_frame;
    const unsigned source=channels==1?0:channel;
    return read16(data+offset+source*2);
  };
  uint64_t cursor=next.next_output_position;
  for(size_t i=0;i<produced;i++,cursor+=sample_rate){
    const uint64_t a=cursor/kOutputRate;
    const unsigned fraction=unsigned(cursor%kOutputRate);
    for(unsigned channel=0;channel<2;channel++){
      const int16_t va=sample(a,channel);
      if(fraction==0){
        queue[original+i*2+channel]=va;
        continue;
      }
      const int16_t vb=sample(a+1,channel);
      const int64_t weighted=int64_t(va)*(kOutputRate-fraction)+
                             int64_t(vb)*fraction;
      // Symmetric nearest-integer rounding preserves signed samples.
      const int64_t rounded=(weighted>=0)
          ?(weighted+int64_t(kOutputRate/2))/int64_t(kOutputRate)
          :(weighted-int64_t(kOutputRate/2))/int64_t(kOutputRate);
      queue[original+i*2+channel]=clamp16(rounded);
    }
  }
  const size_t final_offset=(count-1)*bytes_per_frame;
  next.previous_left=read16(data+final_offset);
  next.previous_right=read16(data+final_offset+(channels==1?0:2));
  next.has_previous=true;
  next.input_frames=end;
  next.next_output_position=position;
  state=next;
  return true;
}
}
