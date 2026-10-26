#pragma once
#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <string>
namespace ptvsourceui {
inline std::string lower(std::string value){
 std::transform(value.begin(),value.end(),value.begin(),
   [](unsigned char c){return (char)std::tolower(c);});
 return value;
}
inline std::string resolution(const ptv::Stream& src){
 const std::string text=lower(src.name+" "+src.title);
 if(text.find("2160p")!=std::string::npos||text.find("4k")!=std::string::npos||
    text.find("uhd")!=std::string::npos)return "4K";
 if(text.find("1080p")!=std::string::npos)return "1080P";
 if(text.find("720p")!=std::string::npos)return "720P";
 if(text.find("480p")!=std::string::npos)return "480P";
 return "UNKNOWN";
}
inline std::string codec(const ptv::Stream& src){
 const std::string text=lower(src.name+" "+src.title);
 if(text.find("av1")!=std::string::npos||text.find("av01")!=std::string::npos)return "AV1";
 if(text.find("vp9")!=std::string::npos)return "VP9";
 if(text.find("h265")!=std::string::npos||text.find("x265")!=std::string::npos||
    text.find("hevc")!=std::string::npos)return "HEVC";
 if(text.find("h264")!=std::string::npos||text.find("x264")!=std::string::npos||
    text.find("avc")!=std::string::npos)return "H264";
 return "CODEC ?";
}
inline bool hdr(const ptv::Stream& src){
 const std::string t=lower(src.name+" "+src.title);
 return t.find("hdr")!=std::string::npos||t.find("dolby vision")!=std::string::npos||
        t.find("dovi")!=std::string::npos;
}
inline std::string label(const ptv::Stream& src){
 std::string out=resolution(src)+"  "+codec(src);
 if(hdr(src))out+="  HDR";
 return out;
}
}
