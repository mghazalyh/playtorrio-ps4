#include "../src/playback_choice.hpp"
#include <cassert>
#include <cstdio>
int main(){
  ptv::Stream mp4,h265,av1,vp9,hls,http,plain;
  mp4.url="https://cdn.example/film.mp4?credential=hidden";
  mp4.title="1080p H264 AAC";
  h265.url="https://cdn.example/film.mkv";
  h265.name="2160p HEVC";
  av1.url="https://cdn.example/film.mkv";
  av1.title="AV1 1080p";
  vp9.url="https://cdn.example/vp9.mkv";
  vp9.name="VP9 720p";
  hls.url="https://cdn.example/playlist.m3u8?hidden=yes";
  http.url="http://cdn.example/movie.mp4";
  plain.url="https://cdn.example/media";
  assert(ptvplayback::score(mp4)>ptvplayback::score(h265));
  assert(ptvplayback::score(h265)>=0);
  assert(ptvplayback::score(plain)>=0);
  assert(ptvplayback::score(av1)<0);
  assert(ptvplayback::score(vp9)<0);
  assert(ptvplayback::score(hls)<0);
  assert(ptvplayback::score(http)<0);
  std::puts("PS4 codec preference tests passed");
}
