#pragma once
// Native PS4-friendly NV12 presentation: crop-aware HD color and smooth
// resampling into packed VideoOut RGB32. No external graphics library.
// Based on public BT.601/BT.709 conversion standards; original implementation.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

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

// PS4 Jaguar and x86-64 shadPS4 hosts support SSE2. Convert eight native
// resolution pixels at a time using signed 32-bit intermediates. Keeping the
// original fixed-point coefficients, rounding, clipping and 0x80 alpha makes
// this bit-exact with the portable scalar path, including out-of-range YUV.
inline int resolve_nv12_pitch(int width,uint32_t suppliedPitch) {
  if(width<2||width>4096)return 0;
  const uint32_t pitch=suppliedPitch?suppliedPitch:
       ((static_cast<uint32_t>(width)+63U)&~63U);
  if(pitch<static_cast<uint32_t>(width)||pitch>8192U)return 0;
  return static_cast<int>(pitch);
}
inline bool native_sse2_enabled() {
#if defined(__SSE2__)
  return true;
#else
  return false;
#endif
}

#if defined(__SSE2__)
inline int native_nv12_row_sse2(const uint8_t* y,const uint8_t* uv,
                               uint32_t* pixels,int width,int ky,int rv,
                               int gu,int gv,int bu,int yOffset) {
  const __m128i zero=_mm_setzero_si128();
  const __m128i chroma128=_mm_set1_epi16(128);
  const __m128i yBias=_mm_set1_epi16(static_cast<short>(yOffset));
  const __m128i uvMask=_mm_set1_epi32(0x0000ffff);
  const __m128i kyMul=_mm_set1_epi32(ky);
  const __m128i rvMul=_mm_set1_epi32(rv);
  const __m128i buMul=_mm_set1_epi32(bu);
  const __m128i greenMul=_mm_set1_epi32((gv<<16)|gu);
  const __m128i round=_mm_set1_epi32(128);
  const __m128i alpha=_mm_set1_epi8(static_cast<char>(0x80));
  int x=0;
  for(;x+7<width;x+=8){
    const __m128i luma=_mm_sub_epi16(
        _mm_unpacklo_epi8(_mm_loadl_epi64(
            reinterpret_cast<const __m128i*>(y+x)),zero),yBias);
    const __m128i chroma=_mm_unpacklo_epi8(_mm_loadl_epi64(
        reinterpret_cast<const __m128i*>(uv+x)),zero);
    const __m128i u=_mm_sub_epi16(_mm_shufflehi_epi16(
        _mm_shufflelo_epi16(_mm_and_si128(chroma,uvMask),
            _MM_SHUFFLE(2,2,0,0)),_MM_SHUFFLE(2,2,0,0)),chroma128);
    const __m128i v=_mm_sub_epi16(_mm_shufflehi_epi16(
        _mm_shufflelo_epi16(_mm_srli_epi32(chroma,16),
            _MM_SHUFFLE(2,2,0,0)),_MM_SHUFFLE(2,2,0,0)),chroma128);
    const __m128i y0=_mm_madd_epi16(_mm_unpacklo_epi16(luma,zero),kyMul);
    const __m128i y1=_mm_madd_epi16(_mm_unpackhi_epi16(luma,zero),kyMul);
    const __m128i r0=_mm_srai_epi32(_mm_add_epi32(
        _mm_add_epi32(y0,_mm_madd_epi16(_mm_unpacklo_epi16(v,zero),rvMul)),
        round),8);
    const __m128i r1=_mm_srai_epi32(_mm_add_epi32(
        _mm_add_epi32(y1,_mm_madd_epi16(_mm_unpackhi_epi16(v,zero),rvMul)),
        round),8);
    const __m128i g0=_mm_srai_epi32(_mm_add_epi32(
        _mm_sub_epi32(y0,_mm_madd_epi16(_mm_unpacklo_epi16(u,v),greenMul)),
        round),8);
    const __m128i g1=_mm_srai_epi32(_mm_add_epi32(
        _mm_sub_epi32(y1,_mm_madd_epi16(_mm_unpackhi_epi16(u,v),greenMul)),
        round),8);
    const __m128i b0=_mm_srai_epi32(_mm_add_epi32(
        _mm_add_epi32(y0,_mm_madd_epi16(_mm_unpacklo_epi16(u,zero),buMul)),
        round),8);
    const __m128i b1=_mm_srai_epi32(_mm_add_epi32(
        _mm_add_epi32(y1,_mm_madd_epi16(_mm_unpackhi_epi16(u,zero),buMul)),
        round),8);
    const __m128i r8=_mm_packus_epi16(_mm_packs_epi32(r0,r1),zero);
    const __m128i g8=_mm_packus_epi16(_mm_packs_epi32(g0,g1),zero);
    const __m128i b8=_mm_packus_epi16(_mm_packs_epi32(b0,b1),zero);
    const __m128i bg=_mm_unpacklo_epi8(b8,g8);
    const __m128i ra=_mm_unpacklo_epi8(r8,alpha);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+x),
                     _mm_unpacklo_epi16(bg,ra));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+x+4),
                     _mm_unpackhi_epi16(bg,ra));
  }
  // Native cropped widths need not be divisible by eight; the caller
  // converts the remaining even number of pixels via the scalar path.
  return x;
}
#endif
inline bool scale_nv12(const uint8_t* data,int width,int height,int pitch,
                       int cropLeft,int cropTop,int visibleW,int visibleH,
                       int outputW,int outputH,bool fullRange,
                       std::vector<uint32_t>& rgb) {
  if(!data||width<2||height<2||width>4096||height>2160||
     pitch<width||pitch>8192||cropLeft<0||cropTop<0||
     (cropLeft&1)||(cropTop&1)||visibleW<2||visibleH<2||
     (visibleW&1)||(visibleH&1)||cropLeft>width-visibleW||
     cropTop>height-visibleH||outputW<2||outputH<2||
     outputW>1920||outputH>1080)return false;
  const uint8_t* y=data+(size_t)cropTop*pitch+cropLeft;
  const uint8_t* uv=data+(size_t)pitch*height+
                     (size_t)(cropTop/2)*pitch+cropLeft;
  rgb.resize((size_t)outputW*outputH);
  // Native-resolution presentation needs no interpolation table. Avoid
  // allocating four temporary sample arrays for every decoded 1080p frame.
  // Preserve every source pixel when the debrid source is 1080p.
  if(outputW==visibleW&&outputH==visibleH){
    const bool hd=visibleH>=720;
    const int ky=fullRange?256:298;
    const int rv=hd?(fullRange?403:459):(fullRange?359:409);
    const int gu=hd?(fullRange?48:55):(fullRange?88:100);
    const int gv=hd?(fullRange?120:136):(fullRange?183:208);
    const int bu=hd?(fullRange?475:541):(fullRange?454:516);
    const int yOffset=fullRange?0:16;
    for(int row=0;row<visibleH;row++){
      const auto* yy=y+(size_t)row*pitch;
      const auto* cc=uv+(size_t)(row/2)*pitch;
      uint32_t* output=rgb.data()+(size_t)row*visibleW;
      // NV12 shares one U/V pair between two horizontal luma samples.
      // Calculate the chroma terms once and convert both pixels exactly.
      int processed=0;
#if defined(__SSE2__)
      processed=native_nv12_row_sse2(yy,cc,output,visibleW,ky,
                                    rv,gu,gv,bu,yOffset);
#endif
      for(int x=processed;x<visibleW;x+=2){
        const int U=(int)cc[x]-128;
        const int V=(int)cc[x+1]-128;
        const int redChroma=rv*V;
        const int greenChroma=gu*U+gv*V;
        const int blueChroma=bu*U;
        for(int lane=0;lane<2;lane++){
          const int Y=ky*((int)yy[x+lane]-yOffset);
          const uint32_t R=(uint32_t)bound8((Y+redChroma+128)>>8);
          const uint32_t G=(uint32_t)bound8((Y-greenChroma+128)>>8);
          const uint32_t B=(uint32_t)bound8((Y+blueChroma+128)>>8);
          output[x+lane]=0x80000000u|(R<<16)|(G<<8)|B;
        }
      }
    }
    return true;
  }
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
