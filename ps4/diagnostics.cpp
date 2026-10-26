#include "diagnostics.hpp"
#include "native_net.hpp"
#include "../vendor/picojson.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <sys/stat.h>
#include <map>

namespace ps4diag {
static FILE* output=nullptr;
static int sequence=0;
bool start_logging(){
  if(output)return true;
  if(::mkdir("/data/playtorrio",0700)<0 && errno!=EEXIST)return false;
  if(::mkdir("/data/playtorrio/logs",0700)<0 && errno!=EEXIST)return false;
  // Never unbounded: rotate logs once per application start.
  struct stat st={};
  if(stat(logfile,&st)==0){
    ::remove(previous);
    ::rename(logfile,previous);
  }
  output=fopen(logfile,"ab");
  if(!output)return false;
  ::chmod(logfile,0600);
  note("BOOT","PlayTorrio PS4 integration diagnostics v0.4; no keys, token strings, URLs, media titles or bodies logged");
  return true;
}
void note(const std::string& component,const std::string& msg){
  if(!output)return;
  // The only callers pass fixed labels, counts and HTTP/error codes.
  // Do not pass arbitrary server output or URLs into this logger.
  const time_t stamp=std::time(nullptr);
  fprintf(output,"[%ld] #%04d [%s] %s\n",(long)stamp,++sequence,
          component.c_str(),msg.c_str());
  fflush(output);  // Survives console crashes/power interruption.
}
static void mark(Summary& s,const char* label,bool ok,const std::string& reason) {
  if(ok)++s.passed;else ++s.failed;
  s.last=std::string(label)+(ok?": OK":": FAILED");
  note(label,std::string(ok?"PASS ":"FAIL ")+reason);
}
static void skip(Summary& s,const char* label,const char* reason){
  ++s.skipped;note(label,std::string("SKIP ")+reason);
}
static bool validJson(const std::string& in,picojson::value& o){
  return picojson::parse(o,in).empty() && o.is<picojson::object>();
}
static std::string getString(const picojson::object& o,const char* name){
  auto i=o.find(name);
  return i!=o.end()&&i->second.is<std::string>()?i->second.get<std::string>():"";
}
static std::string auth(const std::string& token) {
  return "Bearer "+token;
}
static std::map<std::string,std::string> apiHeaders(const std::string& token){
  return {{"Accept","application/vnd.github+json"},
          {"X-GitHub-Api-Version","2022-11-28"},
          {"Authorization",auth(token)}};
}
static bool good(const ps4net::Response& r) {
  return (r.status>=200 && r.status<300) && !r.truncated && !r.platform_error;
}
static std::string outcome(const ps4net::Response& r){
  return "http="+std::to_string(r.status)+" transport="+
    std::to_string(r.platform_error)+" bounded="+std::to_string((int)!r.truncated);
}
Summary run(const ptv::Settings& cfg,bool githubWriteCheck){
  Summary s;
  note("START","Full native network/settings integration suite; only synthetic diagnostics are written");
  int netError=0;
  bool network=ps4net::init(netError);
  mark(s,"HTTPS",network,"native PS4 networking initialization code="+std::to_string(netError));
  if(!network){
    skip(s,"ADDONS","Network not initialized");
    skip(s,"TORBOX","Network not initialized");
    skip(s,"GITHUB","Network not initialized");
    skip(s,"MEDIA","Network not initialized");
    skip(s,"PLAYBACK","No compiled decoding backend");
    note("SUMMARY","passed="+std::to_string(s.passed)+" failed="+std::to_string(s.failed));
    return s;
  }
  // Manifests may encode addon configuration in their path: NEVER print URLs.
  if(cfg.addons.empty())skip(s,"ADDONS","No manifest URLs imported");
  for(size_t i=0;i<cfg.addons.size()&&i<12;i++){
    std::string entry="manifest#"+std::to_string(i+1);
    auto response=ps4net::request(cfg.addons[i],"GET",{}, "",384*1024);
    ptv::Manifest parsed;std::string error;
    const bool valid=good(response) && ptv::parse_manifest(response.body,parsed,error);
    mark(s,"ADDONS",valid,entry+" "+outcome(response)+" schema="+std::to_string((int)valid));
    if(!valid||!parsed.streams)continue;
    if(cfg.diagnostic_id.empty()){
      skip(s,"STREAM","Set diagnosticId in settings to test a real movie/episode");
      continue;
    }
    const std::string url=ptv::stream_url(cfg.addons[i],
        cfg.diagnostic_type,cfg.diagnostic_id);
    if(url.empty()){mark(s,"STREAM",false,"Invalid diagnostic stream path");continue;}
    auto streams=ps4net::request(url,"GET",{},"",768*1024);
    std::string parseError;
    auto result=good(streams)?ptv::parse_streams(streams.body,parsed.name,parseError):std::vector<ptv::Stream>{};
    bool ok=good(streams)&&parseError.empty();
    s.addon_streams+=(int)result.size();
    mark(s,"STREAM",ok,entry+" "+outcome(streams)+" sources="+std::to_string(result.size()));
    // Non-destructive sample transfer check: Range request, never download full movies.
    const auto playable=std::find_if(result.begin(),result.end(),
      [](const ptv::Stream& st){return st.url.rfind("https://",0)==0;});
    if(playable!=result.end()){
      auto sample=ps4net::request(playable->url,"GET",
        {{"Range","bytes=0-1023"}},"",1024);
      mark(s,"CDN",good(sample)||sample.status==206,
        "first streaming source partial transfer "+outcome(sample));
    }else if(!result.empty()){
      skip(s,"CDN","Only infoHash sources returned; TorBox resolution required");
    }
  }
  if(cfg.torbox_api_key.empty())skip(s,"TORBOX","No PS4-local TorBox credential configured");
  else{
    const auto hdr=std::map<std::string,std::string>{
      {"Authorization",auth(cfg.torbox_api_key)},{"Accept","application/json"}};
    auto user=ps4net::request("https://api.torbox.app/v1/api/user/me","GET",hdr,"",256*1024);
    picojson::value reply;
    bool valid=good(user)&&validJson(user.body,reply);
    if(valid) {
      auto info=reply.get<picojson::object>();
      auto value=info.find("success");
      valid=value!=info.end()&&value->second.is<bool>()&&value->second.get<bool>();
    }
    mark(s,"TORBOX",valid,"authenticated user endpoint "+outcome(user));
    auto files=ps4net::request("https://api.torbox.app/v1/api/torrents/mylist",
                               "GET",hdr,"",512*1024);
    mark(s,"TORBOX",good(files),"torrent inventory endpoint "+outcome(files));
    if(cfg.torbox_torrent_id>=0&&cfg.torbox_file_id>=0){
      auto dl=ps4net::request(ptv::torbox_request_url(
            cfg.torbox_torrent_id,cfg.torbox_file_id),"GET",hdr,"",96*1024);
      std::string error;
      auto link=good(dl)?ptv::torbox_extract_link(dl.body,error):std::string{};
      mark(s,"TORBOX_DL",!link.empty(),
           "selected existing TorBox file resolved "+outcome(dl));
      if(!link.empty()){
        auto bytes=ps4net::request(link,"GET",{{"Range","bytes=0-1023"}},"",1024);
        mark(s,"MEDIA",good(bytes)||bytes.status==206,
             "TorBox CDN short partial transfer "+outcome(bytes));
      }
    }else skip(s,"TORBOX_DL","Set existing torboxTorrentId and torboxFileId to test resolution");
  }
  if(cfg.github_token.empty())skip(s,"GITHUB","No PS4-local private repository credential configured");
  else{
    auto url=ptv::github_get_url(cfg);
    const auto headers=apiHeaders(cfg.github_token);
    auto remote=ps4net::request(url,"GET",headers,"",2*1024*1024);
    bool valid=false;
    std::string sha,content;
    if(good(remote)){
      picojson::value response;
      if(validJson(remote.body,response)){
        const auto& fields=response.get<picojson::object>();
        sha=getString(fields,"sha");
        auto encoded=getString(fields,"content");
        content=ptv::base64_decode(encoded);
        std::map<std::string,ptv::LibraryRecord> records;
        std::string parseError;
        valid=!sha.empty()&&ptv::parse_library(content,records,parseError);
        if(valid){
          s.library_titles=(int)std::count_if(records.begin(),records.end(),
            [](const auto& v){return !v.second.deleted;});
          for(const auto& record:records){
            if(record.second.deleted || s.preview_titles.size()>=16)continue;
            picojson::value item;
            if(validJson(record.second.item_json,item)){
              auto title=getString(item.get<picojson::object>(),"title");
              if(!title.empty())s.preview_titles.push_back(title);
            }
          }
          // Keep a private offline cache; original GitHub JSON is preserved.
          // Do not write secrets to disk except the separately imported settings.
          const char* path="/data/playtorrio/library-cache.json.tmp";
          FILE* file=fopen(path,"wb");
          if(file){
            const bool ok=fwrite(content.data(),1,content.size(),file)==content.size();
            fflush(file);fclose(file);
            if(ok){
              chmod(path,0600);
              if(rename(path,"/data/playtorrio/library-cache.json")!=0)
                remove(path);
            }else remove(path);
          }
        }
      }
    }
    mark(s,"GITHUB",valid,"private watchlist read "+outcome(remote)+
      " entries="+std::to_string(s.library_titles));
    if(valid&&githubWriteCheck){
      // No data is modified: PUT the exact fetched bytes guarded by the SHA
      // from that same read. A concurrent edit produces HTTP 409/422.
      auto writeHeaders=headers;
      writeHeaders["Content-Type"]="application/json";
      const std::string data=ptv::github_contents_update(content,sha,cfg.github_branch);
      auto put=ps4net::request(ptv::github_put_url(cfg),"PUT",writeHeaders,data,96*1024);
      mark(s,"GITHUB_WRITE",good(put),
           "SHA-guarded unchanged contents write "+outcome(put));
    }else skip(s,"GITHUB_WRITE","Skipped; read invalid or write check disabled");
  }
  // Avoid pretending native AvPlayer/MPV works: SDK methods have incomplete
  // typed declarations and no media pipeline is included in this PKG.
  skip(s,"PLAYBACK","Video decoder, audio tracks and subtitle renderer not integrated");
  note("SUMMARY","passed="+std::to_string(s.passed)+" failed="+
      std::to_string(s.failed)+" skipped="+std::to_string(s.skipped)+
      " parsed_streams="+std::to_string(s.addon_streams)+
      " library_titles="+std::to_string(s.library_titles));
  return s;
}
}
