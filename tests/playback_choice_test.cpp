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
  assert(ptvplayback::score(h265)<ptvplayback::score(mp4));
  assert(ptvplayback::score(plain)>=0);
  assert(ptvplayback::score(av1)<0);
  assert(ptvplayback::score(vp9)<0);
  assert(ptvplayback::score(hls)<0);
  assert(ptvplayback::score(http)<0);
  // Torrentio offers magnets using infoHash/fileIdx instead of direct URLs.
  // Their quality must be scored, never left at zero or returned in API order.
  ptv::Stream torboxHd,torboxSd,badTorbox,torboxAv1;
  torboxHd.info_hash=std::string(40,'a');
  torboxHd.name="Torrentio 1080p";
  torboxHd.title="1080p x264 AAC .mp4";
  torboxSd.info_hash=std::string(40,'b');
  torboxSd.name="Torrentio 480p";
  torboxSd.title="480p x264 .mp4";
  badTorbox=torboxHd;
  badTorbox.info_hash=std::string(40,'z');
  torboxAv1=torboxHd;
  torboxAv1.name="Torrentio 1080p AV1";
  ptv::Stream directLow;
  directLow.url="https://cdn.example/low.mp4?secret=hidden";
  directLow.title="720p H264 AAC";
  const auto high=ptvplayback::rank(torboxHd,true);
  assert(high.score>0&&!high.direct);
  assert(high.score>ptvplayback::rank(torboxSd,true).score);
  assert(high.score>ptvplayback::rank(directLow,true).score);
  assert(ptvplayback::rank(torboxHd,false).score<0);
  assert(ptvplayback::rank(badTorbox,true).score<0);
  assert(ptvplayback::rank(torboxAv1,true).score<0);
  assert(ptvplayback::rank(mp4,true).direct);
  // Preferred TorBox candidate can still carry a signed direct URL;
  // the selector chooses whichever compatible path has the higher score.
  torboxHd.url="https://cdn.example/video.mp4";
  auto mixed=ptvplayback::rank(torboxHd,true);
  assert(mixed.score>0);
  std::puts("PS4 codec preference tests passed");
}
