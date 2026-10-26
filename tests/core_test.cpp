#include "../src/core.hpp"
#include <cassert>
#include <iostream>
using namespace ptv;
int main() {
  std::string err,url;
  assert(normalize_manifest_url("stremio://addon.example/abc/manifest.json",url));
  assert(url=="https://addon.example/abc/manifest.json");
  assert(!normalize_manifest_url("http://untrusted.example/manifest.json",url));
  assert(!normalize_manifest_url("https://alice:pass@example.com/manifest.json",url));
  Settings s;
  assert(import_settings(R"({"addons":[{"manifestUrl":"https://torrentio.example/abcd/manifest.json"},"stremio://other.example/manifest.json"],"githubRepo":"mghazalyh/torriodb","githubBranch":"main","torboxApiKey":"example"})",s,err));
  assert(s.addons.size()==2);
  assert(s.github_repo=="mghazalyh/torriodb");
  assert(stream_url(s.addons[0],"series","tt456:1:2")=="https://torrentio.example/abcd/stream/series/tt456%3A1%3A2.json");
  Manifest m;assert(parse_manifest(R"({"id":"com.example.stream","name":"Test","types":["movie","series"],"resources":[{"name":"stream","types":["movie"]}]})",m,err));
  assert(m.streams && m.types.size()==2);
  auto streams=parse_streams(R"json({"streams":[{"name":"TorBox","title":"1080p","url":"https://dl.example/video.mkv"},{"name":"Raw magnet","infoHash":"0123456789abcdef0123456789abcdef01234567","fileIdx":4},{"name":"invalid","url":"javascript:alert(1)"}]})json","Torrentio",err);
  assert(streams.size()==2 && streams[0].is_playable() && !streams[1].is_playable());
  assert(streams[1].file_index==4);
  assert(torbox_request_url(2,3).find("torrent_id=2")!=std::string::npos);
  assert(torbox_extract_link(R"({"success":true,"data":"https://cdn.torbox.app/test"})",err)=="https://cdn.torbox.app/test");
  assert(torbox_extract_link(R"({"success":false,"detail":"Token expired"})",err).empty());
  std::map<std::string,LibraryRecord> db;
  assert(parse_library(R"({"version":1,"records":{"imdb:tt1":{"item":{"imdbId":"tt1","title":"Example"},"deleted":false}}})",db,err));
  assert(db.size()==1);
  std::map<std::string,LibraryRecord> pending;
  pending["imdb:tt1"]={"imdb:tt1",db["imdb:tt1"].item_json,true};
  pending["imdb:tt2"]={"imdb:tt2",R"({"imdbId":"tt2","title":"Second"})",false};
  auto merged=merge_library(db,pending);
  assert(merged["imdb:tt1"].deleted && merged.size()==2);
  std::map<std::string,LibraryRecord> decoded;
  assert(parse_library(encode_library(merged),decoded,err) && decoded.size()==2);
  assert(decoded["imdb:tt1"].deleted);
  assert(!parse_library(R"({"version":2,"records":{}})",decoded,err) && decoded.size()==2);
  assert(base64_decode(base64_encode("TorBox test"))=="TorBox test");
  assert(github_contents_update("{}","abcdef","main").find("abcdef")!=std::string::npos);
  std::cout<<"All PS4 platform-independent addon, TorBox, settings and library tests passed\n";
}
