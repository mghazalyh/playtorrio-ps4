#include "source_resolver.hpp"
#include "native_net.hpp"
#include "diagnostics.hpp"
#include "../vendor/picojson.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>
#include <orbis/libkernel.h>
namespace ps4source {
namespace {
bool isSuccess(const picojson::object& o){
  auto k=o.find("success");
  return k!=o.end()&&k->second.is<bool>()&&k->second.get<bool>();
}
std::string text(const picojson::object& o,const char* k){
  auto i=o.find(k);
  return i==o.end()||!i->second.is<std::string>()?"":i->second.get<std::string>();
}
int number(const picojson::object& o,const char* k){
  auto i=o.find(k);return i!=o.end()&&i->second.is<double>()?
      (int)i->second.get<double>():-1;
}
bool parse(const std::string& str,picojson::object& o){
  picojson::value root;
  if(!picojson::parse(root,str).empty()||!root.is<picojson::object>())return false;
  o=root.get<picojson::object>();return true;
}
std::string escape(const std::string& s){
  const char* hx="0123456789ABCDEF";
  std::string result;
  for(unsigned char c:s){
    if(std::isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')result+=(char)c;
    else{result+='%';result+=hx[c>>4];result+=hx[c&15];}
  }
  return result;
}
std::map<std::string,std::string> authorization(const ptv::Settings& s) {
  return {{"Authorization","Bearer "+s.torbox_api_key},
          {"Accept","application/json"}};
}
std::string linkFor(const ptv::Settings& s,int torrent,int file){
  if(torrent<0||file<0||s.torbox_api_key.empty())return {};
  // TorBox's requestdl endpoint requires token in its query per API docs.
  auto url=ptv::torbox_request_url(torrent,file)+"&token="+escape(s.torbox_api_key);
  auto response=ps4net::request(url,"GET",authorization(s),"",128*1024);
  std::string error;
  if(response.status!=200||response.platform_error||response.truncated)return {};
  return ptv::torbox_extract_link(response.body,error);
}
bool mediaFile(const picojson::object& item) {
  auto name=text(item,"name");
  std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
  return name.size()>4 && (
    name.rfind(".mkv")==name.size()-4 ||
    name.rfind(".mp4")==name.size()-4 ||
    name.rfind(".m4v")==name.size()-4 ||
    name.rfind(".ts")==name.size()-3);
}
std::string chooseFile(const ptv::Settings& s,const picojson::value& item,int fileIndex){
  if(!item.is<picojson::object>())return {};
  const auto& torrent=item.get<picojson::object>();
  int torrentId=number(torrent,"id");
  if(torrentId<0)return {};
  auto data=torrent.find("files");
  if(data==torrent.end()||!data->second.is<picojson::array>())return {};
  const auto& files=data->second.get<picojson::array>();
  int chosen=-1;
  if(fileIndex>=0&&fileIndex<(int)files.size()&&files[fileIndex].is<picojson::object>()){
    chosen=number(files[fileIndex].get<picojson::object>(),"id");
  }
  if(chosen<0) {
    int64_t biggest=0;
    for(const auto& file:files){
      if(!file.is<picojson::object>())continue;
      const auto& object=file.get<picojson::object>();
      if(!mediaFile(object))continue;
      int64_t bytes=0;
      auto sz=object.find("size");if(sz!=object.end()&&sz->second.is<double>())
        bytes=(int64_t)sz->second.get<double>();
      int id=number(object,"id");
      if(id>=0&&bytes>biggest){biggest=bytes;chosen=id;}
    }
  }
  if(chosen<0)return {};
  return linkFor(s,torrentId,chosen);
}
std::string torboxHash(const ptv::Settings& s,const ptv::Stream& stream){
  if(s.torbox_api_key.empty()||stream.info_hash.size()!=40)return {};
  std::string hash=stream.info_hash;
  if(!std::all_of(hash.begin(),hash.end(),[](char c){return std::isxdigit((unsigned char)c);}))return {};
  const std::string magnet="magnet:?xt=urn:btih:"+hash;
  const auto headers=std::map<std::string,std::string>{
    {"Authorization","Bearer "+s.torbox_api_key},
    {"Content-Type","application/x-www-form-urlencoded"},
    {"Accept","application/json"}};
  // Use existing TorBox cache if available, otherwise request this magnet.
  int torrentId=-1;
  auto current=ps4net::request("https://api.torbox.app/v1/api/torrents/mylist",
                              "GET",headers,"",1024*1024);
  if(current.status==200){
    picojson::object root;
    if(parse(current.body,root)&&isSuccess(root)){
      auto i=root.find("data");
      if(i!=root.end()&&i->second.is<picojson::array>())
        for(const auto& item:i->second.get<picojson::array>()){
          if(!item.is<picojson::object>())continue;
          auto& t=item.get<picojson::object>();
          auto h=text(t,"hash");
          std::transform(h.begin(),h.end(),h.begin(),[](unsigned char c){return std::tolower(c);});
          std::string lower=hash;
          std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return std::tolower(c);});
          if(h==lower) {
            std::string link=chooseFile(s,item,stream.file_index);
            if(!link.empty())return link;
            torrentId=number(t,"id");break;
          }
        }
    }
  }
  if(torrentId<0){
    const auto create=ps4net::request("https://api.torbox.app/v1/api/torrents/createtorrent",
                       "POST",headers,"magnet="+escape(magnet),64*1024);
    picojson::object data;
    if(create.status!=200||!parse(create.body,data)||!isSuccess(data)){
      ps4diag::note("TORBOX","FAIL magnet creation HTTP="+std::to_string(create.status));
      return {};
    }
    auto item=data.find("data");
    if(item!=data.end()&&item->second.is<picojson::object>())
      torrentId=number(item->second.get<picojson::object>(),"torrent_id");
    ps4diag::note("TORBOX","INFO magnet added or queued on account (no URL logged)");
  }
  if(torrentId<0)return {};
  for(int attempt=0;attempt<6;attempt++){
    const auto url="https://api.torbox.app/v1/api/torrents/mylist?id="+std::to_string(torrentId)+"&bypass_cache=true";
    auto response=ps4net::request(url,"GET",headers,"",512*1024);
    picojson::object info;
    if(response.status==200&&parse(response.body,info)&&isSuccess(info)){
      auto d=info.find("data");
      if(d!=info.end()){
        std::string link=chooseFile(s,d->second,stream.file_index);
        if(!link.empty())return link;
      }
    }
    sceKernelUsleep(1200000);
  }
  return {};
}
}
Selection resolve(const ptv::Settings& s,const std::string& mediaType,
                  const std::string& mediaId){
  Selection chosen;
  if(!s.direct_stream_url.empty()){
    chosen.url=s.direct_stream_url;
    chosen.reason="Local direct HTTPS stream";
    ps4diag::note("RESOLVE","PASS direct video configured");
    return chosen;
  }
  if(s.torbox_torrent_id>=0&&s.torbox_file_id>=0){
    chosen.url=linkFor(s,s.torbox_torrent_id,s.torbox_file_id);
    if(!chosen.url.empty()){
      chosen.from_torbox=true;chosen.reason="Existing TorBox file";
      ps4diag::note("RESOLVE","PASS existing TorBox file resolved");
      return chosen;
    }
    ps4diag::note("RESOLVE","FAIL existing TorBox file resolution");
  }
  auto type=mediaType.empty()?s.diagnostic_type:mediaType;
  auto id=mediaId.empty()?s.diagnostic_id:mediaId;
  if(id.empty()){chosen.reason="Set diagnosticId in settings or select a My List movie";return chosen;}
  // Prefer same ordered addon list as Windows/iPad Stremio configs.
  for(const auto& addon:s.addons){
    const std::string request=ptv::stream_url(addon,type,id);
    if(request.empty())continue;
    auto response=ps4net::request(request,"GET",{},"",768*1024);
    if(response.status!=200||response.truncated||response.platform_error)continue;
    std::string error;
    auto streams=ptv::parse_streams(response.body,addon,error);
    chosen.sources+=(int)streams.size();
    for(const auto& stream:streams){
      const auto cleanPath=stream.url.substr(0,stream.url.find('?'));
      if(stream.is_playable()&&stream.url.rfind("https://",0)==0 &&
         cleanPath.find(".m3u8")==std::string::npos){
        chosen.url=stream.url;chosen.reason="Direct addon HTTP video";
        ps4diag::note("RESOLVE","PASS addon returned playable HTTPS stream");
        return chosen;
      }
    }
    for(const auto& stream:streams){
      if(!s.torbox_api_key.empty()&&stream.info_hash.size()==40){
        chosen.url=torboxHash(s,stream);
        if(!chosen.url.empty()){
          chosen.from_torbox=true;chosen.reason="Torrentio via TorBox";
          ps4diag::note("RESOLVE","PASS addon torrent resolved using TorBox");
          return chosen;
        }
      }
    }
  }
  chosen.reason="No compatible stream found (or TorBox download is not cached)";
  ps4diag::note("RESOLVE","FAIL no compatible source; no private URL logged");
  return chosen;
}
}
