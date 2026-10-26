#include "../src/source_badges.hpp"
#include <cassert>
#include <cstdio>
int main(){
 ptv::Stream s;
 s.name="Torrentio";s.title="Movie 1080p H264 AAC";
 assert(ptvsourceui::resolution(s)=="1080P");
 assert(ptvsourceui::codec(s)=="H264");
 assert(!ptvsourceui::hdr(s));
 s.title="4K HEVC HDR";
 assert(ptvsourceui::resolution(s)=="4K");
 assert(ptvsourceui::codec(s)=="HEVC");
 assert(ptvsourceui::hdr(s));
 assert(ptvsourceui::label(s).find("HDR")!=std::string::npos);
 s.title="Unexpected encode";
 assert(ptvsourceui::resolution(s)=="UNKNOWN");
 assert(ptvsourceui::codec(s)=="CODEC ?");
 std::puts("Source picker codec, quality and HDR metadata badges passed");
}
