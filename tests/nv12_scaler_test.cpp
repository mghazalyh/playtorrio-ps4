#include "../src/nv12_scaler.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
  std::vector<uint32_t> pixels;
  // A 4x4 black limited-range NV12 picture with neutral chroma.
  uint8_t black[24]={16,16,16,16,16,16,16,16,
                     16,16,16,16,16,16,16,16,
                     128,128,128,128,128,128,128,128};
  assert(ptvvideo::scale_nv12(black,4,4,4,0,0,4,4,8,8,false,pixels));
  assert(pixels.size()==64);
  for(uint32_t p:pixels)assert(p==0x80000000u);
  uint8_t white[24]={235,235,235,235,235,235,235,235,
                     235,235,235,235,235,235,235,235,
                     128,128,128,128,128,128,128,128};
  assert(ptvvideo::scale_nv12(white,4,4,4,0,0,4,4,8,8,false,pixels));
  for(uint32_t p:pixels)assert((p&0x00FFFFFFu)==0x00FFFFFFu);
  // Gradient must vary per output pixel, unlike old 2x2 block rendering.
  uint8_t grad[24]={16,64,128,235,16,64,128,235,
                    16,64,128,235,16,64,128,235,
                    128,128,128,128,128,128,128,128};
  assert(ptvvideo::scale_nv12(grad,4,4,4,0,0,4,4,8,8,false,pixels));
  assert(pixels[2]!=pixels[3]);
  assert(pixels[3]!=pixels[4]);
  // Crop ignores padded decoder edges and uses true plane height.
  uint8_t pad[48]={};
  for(int row=0;row<4;row++)for(int col=0;col<8;col++)pad[row*8+col]=235;
  for(int row=0;row<2;row++)for(int col=0;col<8;col++)pad[4*8+row*8+col]=128;
  assert(ptvvideo::scale_nv12(pad,4,4,8,0,0,4,4,4,4,false,pixels));
  for(uint32_t p:pixels)assert((p&0x00FFFFFFu)==0x00FFFFFFu);
  assert(!ptvvideo::scale_nv12(nullptr,4,4,4,0,0,4,4,4,4,false,pixels));
  assert(!ptvvideo::scale_nv12(black,4,4,4,1,0,2,2,4,4,false,pixels));
  assert(!ptvvideo::scale_nv12(black,4,4,4,0,0,4,4,1400,800,false,pixels));
  // Full-HD decoder frames must preserve native image pixels rather than
  // collapse a 1080p debrid stream to 720p.
  std::vector<uint8_t> hd((size_t)1920*1080*3/2,128);
  std::fill(hd.begin(),hd.begin()+(size_t)1920*1080,235);
  assert(ptvvideo::scale_nv12(hd.data(),1920,1080,1920,0,0,
      1920,1080,1920,1080,false,pixels));
  assert(pixels.size()==(size_t)1920*1080);
  assert((pixels[0]&0x00FFFFFFu)==0x00FFFFFFu);
  assert((pixels.back()&0x00FFFFFFu)==0x00FFFFFFu);
  // The 720p fallback image is still valid for older source resolutions.
  assert(ptvvideo::scale_nv12(hd.data(),1920,1080,1920,0,0,
      1920,1080,1280,720,false,pixels));
  assert(!ptvvideo::scale_nv12(hd.data(),1920,1080,1920,0,0,
      1920,1080,2048,1152,false,pixels));
  puts("Native 1080p pixel fidelity and bilinear regression passed");
}
