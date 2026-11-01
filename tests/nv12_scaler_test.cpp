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
  assert(!ptvvideo::scale_nv12(black,4,4,4,0,0,4,4,2048,1152,false,pixels));
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
  // Reference the original per-pixel equation independently: hoisting
  // chroma terms must remain byte-identical for both BT.601 and BT.709.
  const auto expected=[](int y,int u,int v,bool hd,bool full)->uint32_t {
    const int ky=full?256:298;
    const int rv=hd?(full?403:459):(full?359:409);
    const int gu=hd?(full?48:55):(full?88:100);
    const int gv=hd?(full?120:136):(full?183:208);
    const int bu=hd?(full?475:541):(full?454:516);
    const int value=y-(full?0:16);
    const int U=u-128,V=v-128;
    const auto clip=[](int n)->uint32_t {
      return (uint32_t)std::max(0,std::min(255,n));
    };
    const uint32_t red=clip((ky*value+rv*V+128)>>8);
    const uint32_t green=clip((ky*value-gu*U-gv*V+128)>>8);
    const uint32_t blue=clip((ky*value+bu*U+128)>>8);
    return 0x80000000u|(red<<16)|(green<<8)|blue;
  };
  constexpr int stride=16,planeW=12,planeH=8;
  std::vector<uint8_t> cropped((size_t)stride*planeH*3/2,128);
  for(int row=0;row<planeH;row++)
    for(int col=0;col<planeW;col++)
      cropped[(size_t)row*stride+col]=(uint8_t)((30+row*25+col*7)%256);
  for(int row=0;row<planeH/2;row++)
    for(int col=0;col<planeW/2;col++){
      const size_t i=(size_t)stride*planeH+(size_t)row*stride+2*col;
      cropped[i]=(uint8_t)(16+row*30+col*13);
      cropped[i+1]=(uint8_t)(240-row*20-col*11);
    }
  for(bool full : {false,true}){
    // Nonzero top/left crop with padded input pitch; no interpolator needed.
    assert(ptvvideo::scale_nv12(cropped.data(),planeW,planeH,stride,
        2,2,8,4,8,4,full,pixels));
    for(int row=0;row<4;row++)
      for(int col=0;col<8;col++){
        const int inY=row+2,inX=col+2;
        const int Y=cropped[(size_t)inY*stride+inX];
        const size_t uv=(size_t)stride*planeH+(size_t)(inY/2)*stride+
                        (size_t)(inX&~1);
        assert(pixels[(size_t)row*8+col] ==
               expected(Y,cropped[uv],cropped[uv+1],false,full));
      }
  }
  // Full-HD non-neutral chroma, BT.709 limited and full-range values.
  const size_t uvBase=(size_t)1920*1080;
  hd[uvBase]=16;hd[uvBase+1]=240;
  for(bool full : {false,true}){
    assert(ptvvideo::scale_nv12(hd.data(),1920,1080,1920,0,0,
        1920,1080,1920,1080,full,pixels));
    assert(pixels[0]==expected(235,16,240,true,full));
    assert(pixels[1]==pixels[0]);
    assert(pixels[2]==expected(235,128,128,true,full));
  }
  puts("Native 1080p pixel fidelity and bilinear regression passed");
}
