#pragma once
// TorBox / addon streams contain *original* media. Rank by native decoder
// compatibility and 1080p detail, never transcode or rewrite their bytes.
#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace ptvplayback {
inline std::string lower(std::string s){
  std::transform(s.begin(),s.end(),s.begin(),
    [](unsigned char c){return (char)std::tolower(c);});
  return s;
}
inline bool contains(const std::string& source,const char* needle){
  return source.find(needle)!=std::string::npos;
}
inline bool supported_metadata(const std::string& description) {
  const auto s=lower(description);
  return !contains(s,"av1")&&!contains(s,"av01")&&
         !contains(s,"vp9")&&!contains(s,".m3u8");
}
inline int quality(const std::string& title,const std::string& file){
  const std::string words=lower(title+" "+file);
  if(!supported_metadata(words))return -1000000;
  int result=100;
  // AVPlayer is much more reliable with MP4 AVC/AAC than MKV/HEVC.
  if(contains(words,".mp4")||contains(words,".m4v"))result+=75;
  else if(contains(words,".mkv"))result+=5;
  else if(contains(words,".ts"))result-=10;
  if(contains(words,"h264")||contains(words,"h.264")||
     contains(words,"x264")||contains(words,"avc"))result+=65;
  if(contains(words,"h265")||contains(words,"h.265")||
     contains(words,"x265")||contains(words,"hevc"))result-=50;
  if(contains(words,"aac"))result+=18;
  if(contains(words,"1080p")||contains(words,"1920x1080")||
     contains(words,"fhd"))result+=95;
  else if(contains(words,"720p")||contains(words,"1280x720"))result+=12;
  else if(contains(words,"480p")||contains(words,"360p"))result-=150;
  if(contains(words,"2160p")||contains(words,"4k")||
     contains(words,"uhd"))result-=110;
  if(contains(words,"hdr")||contains(words,"dovi")||
     contains(words,"dolby vision")||contains(words,"10bit")||
     contains(words,"10-bit"))result-=32;
  if(contains(words,"camrip")||contains(words,"hdcam")||
     contains(words,"telesync")||contains(words,"cam "))result-=250;
  return result;
}
inline int score(const ptv::Stream& stream){
  if(stream.url.rfind("https://",0)!=0)return -1000000;
  const auto path=stream.url.substr(0,stream.url.find('?'));
  if(contains(lower(path),".m3u8"))return -1000000;
  return quality(stream.name+" "+stream.title,path);
}
inline int score_torbox(const ptv::Stream& stream){
  if(stream.info_hash.size()!=40||
     !std::all_of(stream.info_hash.begin(),stream.info_hash.end(),
       [](unsigned char c){return std::isxdigit(c)!=0;}))
    return -1000000;
  // TorBox is a debrid transport, not a transcoder. Give equivalent-quality
  // TorBox streams a modest preference without overvaluing poor codecs.
  const int metadata=quality(stream.name+" "+stream.title,"");
  return metadata<0?metadata:metadata+20;
}
// Apply the same source-quality policy to the on-screen Torrentio picker
// and automatic fallback. A TorBox magnet is a stream candidate, not a
// zero-quality placeholder: ranking it by return order can choose blurry SD.
struct RankedSource {
  int score=-1000000;
  bool direct=false;
};
inline RankedSource rank(const ptv::Stream& stream,bool torboxConfigured){
  const int directScore=score(stream);
  const int torboxScore=torboxConfigured?score_torbox(stream):-1000000;
  if(directScore<0&&torboxScore<0)return {};
  if(directScore>=torboxScore&&directScore>=0)return {directScore,true};
  if(torboxScore>=0)return {torboxScore,false};
  return {};
}
inline int score_file(const std::string& name,int64_t bytes){
  const std::string path=lower(name);
  if(!(contains(path,".mp4")||contains(path,".m4v")||
       contains(path,".mkv")||contains(path,".ts")))return -1000000;
  int result=quality(name,path);
  if(result<0)return result;
  // Size matters only within the same advertised quality class, not
  // enough to promote incompatible 4K HEVC over H.264 1080p.
  if(bytes>0){
    const int64_t mib=bytes/(1024LL*1024LL);
    result+=(int)std::min<int64_t>(mib/512,12);
  }
  return result;
}
}
