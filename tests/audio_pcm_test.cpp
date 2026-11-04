#include "../src/audio_pcm.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

static void append16(std::vector<uint8_t>& target,int16_t value) {
  const uint16_t bits=uint16_t(value);
  target.push_back(uint8_t(bits));
  target.push_back(uint8_t(bits>>8));
}
static std::vector<uint8_t> makePcm(size_t frames,unsigned channels) {
  std::vector<uint8_t> pcm;
  for(size_t i=0;i<frames;i++){
    for(unsigned c=0;c<channels;c++){
      const int sample=int((i*67+c*131)%62001)-31000;
      append16(pcm,int16_t(sample));
    }
  }
  return pcm;
}
static void testChunkContinuity(unsigned channels,unsigned rate,size_t frames) {
  const auto pcm=makePcm(frames,channels);
  ptvaudio::ResampleState onceState{},chunkState{};
  std::vector<int16_t> oneShot,chunked;
  assert(ptvaudio::append_pcm48_stereo(pcm.data(),pcm.size(),
                                      channels,rate,onceState,oneShot,16384));
  size_t pos=0;
  constexpr size_t sizes[]={1,17,101,3,443,2,19,61};
  unsigned step=0;
  while(pos<frames){
    const size_t count=std::min(sizes[step%8],frames-pos);
    const uint8_t* data=pcm.data()+pos*channels*2;
    assert(ptvaudio::append_pcm48_stereo(data,count*channels*2,
                                        channels,rate,chunkState,chunked,16384));
    pos+=count;
    step++;
  }
  assert(chunked==oneShot);
  assert(chunkState.input_frames==frames);
  assert(chunkState.next_output_position==onceState.next_output_position);
}
int main() {
  ptvaudio::ResampleState state{};
  std::vector<int16_t> pending;
  std::vector<uint8_t> stereo;
  for(int i=0;i<1024;i++){
    append16(stereo,int16_t(i-512));
    append16(stereo,int16_t(512-i));
  }
  assert(ptvaudio::append_pcm48_stereo(stereo.data(),stereo.size(),
                                      2,48000,state,pending));
  assert(pending.size()==2048);
  assert(pending[0]==-512&&pending[1]==512);
  assert(pending[2046]==511&&pending[2047]==-511);
  pending.clear();
  std::vector<uint8_t> mono;
  for(int i=0;i<441;i++)append16(mono,int16_t(i));
  assert(ptvaudio::append_pcm48_stereo(mono.data(),mono.size(),
                                      1,44100,state,pending));
  assert(pending.size()==958); // last interpolated sample waits for next chunk
  for(size_t i=0;i<pending.size();i+=2)assert(pending[i]==pending[i+1]);
  pending.clear();
  assert(ptvaudio::append_pcm48_stereo(mono.data(),mono.size(),
                                      1,44100,state,pending));
  assert(pending.size()==960);
  pending.clear();
  assert(!ptvaudio::append_pcm48_stereo(mono.data(),mono.size(),
                                       0,44100,state,pending));
  assert(!ptvaudio::append_pcm48_stereo(mono.data(),mono.size(),
                                       1,0,state,pending));
  const uint64_t offset=state.input_frames;
  const uint64_t phase=state.next_output_position;
  assert(!ptvaudio::append_pcm48_stereo(mono.data(),mono.size(),
                                       1,44100,state,pending,32));
  assert(pending.empty());
  assert(state.input_frames==offset&&state.next_output_position==phase);
  assert(!ptvaudio::append_pcm48_stereo(mono.data(),mono.size()-1,
                                       1,44100,state,pending));
  assert(state.input_frames==offset&&state.next_output_position==phase);
  std::vector<uint8_t> surround;
  for(int i=0;i<16;i++){
    append16(surround,100);
    append16(surround,-100);
    for(int ch=2;ch<6;ch++)append16(surround,900);
  }
  assert(ptvaudio::append_pcm48_stereo(surround.data(),surround.size(),
                                      6,48000,state,pending));
  assert(pending.size()==32&&pending[0]==100&&pending[1]==-100);
  pending.clear();
  std::vector<uint8_t> extreme;
  append16(extreme,-32768);
  append16(extreme,32767);
  append16(extreme,-1);
  append16(extreme,1);
  assert(ptvaudio::append_pcm48_stereo(extreme.data(),extreme.size(),
                                      2,48000,state,pending));
  assert(pending.size()==4);
  assert(pending[0]==-32768&&pending[1]==32767);
  assert(pending[2]==-1&&pending[3]==1);
  pending.clear();
  assert(!ptvaudio::append_pcm48_stereo(nullptr,stereo.size(),
                                       2,48000,state,pending));
  testChunkContinuity(2,48000,4096);
  testChunkContinuity(1,44100,4410);
  testChunkContinuity(2,44100,4410);
  testChunkContinuity(2,32000,3200);
  testChunkContinuity(2,96000,4096);
  testChunkContinuity(6,44100,4410);
  return 0;
}
