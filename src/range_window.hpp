#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>

namespace ptvrange {
// Reference-inspired bounded, seekable linear read-ahead. The native worker
// serves AvPlayer from a sliding window, never by downloading a full video.
constexpr uint64_t kBlock=2ULL*1024*1024;
constexpr uint64_t kAhead=2;
constexpr uint64_t kBehind=1;
constexpr uint64_t kNone=std::numeric_limits<uint64_t>::max();

inline uint64_t block_count(uint64_t bytes) {
  return bytes/kBlock+((bytes%kBlock)!=0);
}
inline uint64_t block_for(uint64_t pos) { return pos/kBlock; }
inline bool keep(uint64_t block,uint64_t wanted) {
  return block<=wanted+kAhead && block+kBehind>=wanted;
}
inline uint64_t choose(uint64_t wanted,uint64_t count,
                       const std::set<uint64_t>& cached,
                       uint64_t suppressed=kNone) {
  if(wanted>=count)return kNone;
  // Decoder reads always take priority over speculative prefetch.
  if(cached.count(wanted)==0)return wanted;
  for(uint64_t i=1;i<=kAhead;i++){
    if(i>count-1-wanted)break;
    const uint64_t block=wanted+i;
    if(block!=suppressed && cached.count(block)==0)return block;
  }
  return kNone;
}
// Strict RFC 7233 Content-Range (not the unbounded server body).
inline bool parse_content_range(const std::string& text,
                                uint64_t& first,uint64_t& last,
                                uint64_t& total) {
  if(text.compare(0,6,"bytes ")!=0)return false;
  size_t p=6;
  auto parse=[&](uint64_t& dst)->bool{
    if(p==text.size()||text[p]<'0'||text[p]>'9')return false;
    uint64_t val=0;
    while(p<text.size()&&text[p]>='0'&&text[p]<='9'){
      const uint64_t digit=(uint64_t)(text[p]-'0');
      if(val>(kNone-digit)/10)return false;
      val=val*10+digit;++p;
    }
    dst=val;return true;
  };
  return parse(first)&&p<text.size()&&text[p++]=='-'&&
         parse(last)&&p<text.size()&&text[p++]=='/'&&
         parse(total)&&p==text.size()&&
         total>0&&total<=(uint64_t)std::numeric_limits<int64_t>::max()&&
         first<=last&&last<total;
}
}
