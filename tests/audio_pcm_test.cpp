#include "../src/audio_pcm.hpp"
#include <cassert>
#include <cstdint>
#include <vector>

static void append16(std::vector<uint8_t>& target,int16_t value) {
  const uint16_t bits=uint16_t(value);
  target.push_back(uint8_t(bits));
  target.push_back(uint8_t(bits>>8));
}
int main() {
  ptvaudio::ResampleState state{};
  std::vector<int16_t> pending;
  std::vector<uint8_t> stereo;
  for(int i=0;i<1024;i++) {
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
  assert(pending.size()==960);
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
  assert(!ptvaudio::append_pcm48_stereo(mono.data(),mono.size(),
                                       1,44100,state,pending,32));
  assert(pending.empty());
  std::vector<uint8_t> surround;
  for(int i=0;i<16;i++) {
    append16(surround,100);
    append16(surround,-100);
    for(int ch=2;ch<6;ch++)append16(surround,900);
  }
  assert(ptvaudio::append_pcm48_stereo(surround.data(),surround.size(),
                                      6,48000,state,pending));
  assert(pending.size()==32&&pending[0]==100&&pending[1]==-100);
  return 0;
}
