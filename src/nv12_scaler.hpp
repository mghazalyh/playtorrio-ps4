#pragma once
// Native PS4-friendly NV12 presentation: crop-aware HD color and smooth
// resampling into packed VideoOut RGB32. No external graphics library.
// Based on public BT.601/BT.709 conversion standards; original implementation.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ptvvideo {
struct Sample {
  int first=0;
  int second=0;
  int weight=0; // 0..256, fractional distance to second
};
inline Sample sample_at(int dst,int dstCount,int srcCount) {
  const int64_t pos=((int64_t)(2*dst+1)*srcCount*256)/(2*dstCount)-128;
  if(pos<=0)return {0,0,0};
  const int64_t max=(int64_t)(srcCount-1)*256;
  if(pos>=max)return {srcCount-1,srcCount-1,0};
  const int base=(int)(pos/256);
  return {base,base+1,(int)(pos%256)};
}
inline int mix8(int a,int b,int w) {
  return (a*(256-w)+b*w+128)>>8;
}
inline int bound8(int v) {
  return std::max(0,std::min(255,v));
}
inline bool scale_nv12(const uint8_t* data,int width,int height,int pitch,
                       int cropLeft,int cropTop,int visibleW,int visibleH,
                       int outputW,int outputH,bool fullRange,
                       std::vector<uint32_t>& rgb) {
  if(!data||width<2||height<2||width>4096||height>2160||
     pitch<width||pitch>8192||cropLeft<0||cropTop<0||
     (cropLeft&1)||(cropTop&1)||visibleW<2||visibleH<2||
     (visibleW&1)||(visibleH&1)||cropLeft>width-visibleW||
     cropTop>height-visibleH||outputW<2||outputH<2||
     outputW>1280||outputH>720)return false;
  const uint8_t* y=data+(size_t)cropTop*pitch+cropLeft;
  const uint8_t* uv=data+(size_t)pitch*height+
                     (size_t)(cropTop/2)*pitch+cropLeft;
  const int chromaW=visibleW/2,chromaH=visibleH/2;
  std::vector<Sample> xs((size_t)outputW),us((size_t)outputW);
  std::vector<Sample> ys((size_t)outputH),vs((size_t)outputH);
  for(int x=0;x<outputW;x++){
    xs[x]=sample_at(x,outputW,visibleW);
    us[x]=sample_at(x,outputW,chromaW);
  }
  for(int yy=0;yy<outputH;yy++){
    ys[yy]=sample_at(yy,outputH,visibleH);
    vs[yy]=sample_at(yy,outputH,chromaH);
  }
  rgb.resize((size_t)outputW*outputH);
  // Limited/full RGB coefficients for HD BT.709 or SD BT.601.
  const bool hd=visibleH>=720;
  const int ky=fullRange?256:298;
  const int rv=hd?(fullRange?403:459):(fullRange?359:409);
  const int gu=hd?(fullRange?48:55):(fullRange?88:100);
  const int gv=hd?(fullRange?120:136):(fullRange?183:208);
  const int bu=hd?(fullRange?475:541):(fullRange?454:516);
  const int yOffset=fullRange?0:16;
  for(int row=0;row<outputH;row++){
    const Sample yy=ys[row],cy=vs[row];
    const uint8_t* y0=y+(size_t)yy.first*pitch;
    const uint8_t* y1=y+(size_t)yy.second*pitch;
    const uint8_t* uv0=uv+(size_t)cy.first*pitch;
    const uint8_t* uv1=uv+(size_t)cy.second*pitch;
    for(int col=0;col<outputW;col++){
      const Sample xx=xs[col],cx=us[col];
      const int l0=mix8(y0[xx.first],y0[xx.second],xx.weight);
      const int l1=mix8(y1[xx.first],y1[xx.second],xx.weight);
      const int Y=mix8(l0,l1,yy.weight)-yOffset;
      const int u0=mix8(uv0[2*cx.first],uv0[2*cx.second],cx.weight);
      const int u1=mix8(uv1[2*cx.first],uv1[2*cx.second],cx.weight);
      const int v0=mix8(uv0[2*cx.first+1],uv0[2*cx.second+1],cx.weight);
      const int v1=mix8(uv1[2*cx.first+1],uv1[2*cx.second+1],cx.weight);
      const int U=mix8(u0,u1,cy.weight)-128;
      const int V=mix8(v0,v1,cy.weight)-128;
      const uint32_t R=(uint32_t)bound8((ky*Y+rv*V+128)>>8);
      const uint32_t G=(uint32_t)bound8((ky*Y-gu*U-gv*V+128)>>8);
      const uint32_t B=(uint32_t)bound8((ky*Y+bu*U+128)>>8);
      rgb[(size_t)row*outputW+col]=0x80000000u|(R<<16)|(G<<8)|B;
    }
  }
  return true;
}
} // namespace ptvvideo
