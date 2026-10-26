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
  puts("HD NV12 color and bilinear scaling regression passed");
}
