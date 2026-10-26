#pragma once
// A shared, conservative playback source preference. Does not guarantee codec
// compatibility; actual streams remain verified by native AvPlayer.
#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <string>
namespace ptvplayback {
inline int score(const ptv::Stream& stream){
  if(stream.url.rfind("https://",0)!=0)return -1000000;
  std::string description=stream.name+" "+stream.title;
  std::transform(description.begin(),description.end(),description.begin(),
    [](unsigned char c){return (char)std::tolower(c);});
  std::string path=stream.url.substr(0,stream.url.find('?'));
  std::transform(path.begin(),path.end(),path.begin(),
    [](unsigned char c){return (char)std::tolower(c);});
  const auto has=[&](const char* needle){
    return description.find(needle)!=std::string::npos;
  };
  if(path.find(".m3u8")!=std::string::npos||has("av1")||
     has("av01")||has("vp9"))return -1000000;
  int result=100;
  if(path.find(".mp4")!=std::string::npos||
     path.find(".m4v")!=std::string::npos)result+=40;
  else if(path.find(".mkv")!=std::string::npos)result+=8;
  if(has("h264")||has("h.264")||has("x264")||has("avc"))result+=65;
  if(has("h265")||has("h.265")||has("x265")||has("hevc"))result+=15;
  if(has("aac"))result+=12;
  if(has("1080p"))result+=35;
  else if(has("720p"))result+=25;
  if(has("2160p")||has("4k")||has("uhd"))result-=90;
  if(has("hdr")||has("dolby vision")||has("dovi"))result-=20;
  return result;
}
}
