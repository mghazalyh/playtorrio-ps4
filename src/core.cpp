#include "core.hpp"
#include "../vendor/picojson.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ptv {
static std::string field(const picojson::object& o, const char* name) {
  auto it=o.find(name);
  return it!=o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : "";
}
static bool parse_object(const std::string& src, picojson::object& result, std::string& error) {
  picojson::value v;
  error=picojson::parse(v,src);
  if(!error.empty()) return false;
  if(!v.is<picojson::object>()){error="Expected a JSON object";return false;}
  result=v.get<picojson::object>();return true;
}
static bool allowed(const std::string& s) {return !s.empty()&&s.find("..")==std::string::npos&&s.find('/')==std::string::npos&&s.find('?')==std::string::npos&&s.find('#')==std::string::npos;}
static std::string enc(const std::string& v){
  static const char* hx="0123456789ABCDEF";std::string out;
  for(unsigned char c:v) if(std::isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out+=(char)c;
  else {out+='%';out+=hx[c>>4];out+=hx[c&15];}return out;
}
bool normalize_manifest_url(const std::string& input,std::string& output) {
  auto s=input;
  if(s.find("stremio://")==0)s="https://"+s.substr(10);
  if(s.find("https://")!=0)return false;
  if(s.find_first_of(" \r\n\t")!=std::string::npos)return false;
  if(s.find('#')!=std::string::npos)return false;
  auto hostend=s.find('/',8);
  if(hostend==8 || s.find('@',8)<hostend)return false; // no URL credentials
  if(s.size()>2048)return false;
  if(s.find("?",8)!=std::string::npos)return false;
  while(!s.empty()&&s.back()=='/')s.pop_back();
  if(s.size()<8)return false;
  if(s.size()<14||s.substr(s.size()-14)!="/manifest.json")s+="/manifest.json";
  output=s;return true;
}
bool import_settings(const std::string& json,Settings& output,std::string& error) {
  picojson::object o;if(!parse_object(json,o,error))return false;
  Settings s;
  auto add=[&](const picojson::value& v){
    std::string url;
    if(v.is<std::string>())url=v.get<std::string>();
    if(v.is<picojson::object>()){
      auto a=v.get<picojson::object>();
      for(auto k:{"manifestUrl","manifest_url","transportUrl","url"}){
        url=field(a,k);if(!url.empty())break;
      }
    }
    std::string normalized;
    if(normalize_manifest_url(url,normalized) &&
       std::find(s.addons.begin(),s.addons.end(),normalized)==s.addons.end())
      s.addons.push_back(normalized);
  };
  for(auto key:{"addons","addonManifests","installedAddons"}) {
    auto it=o.find(key);
    if(it!=o.end()&&it->second.is<picojson::array>())
      for(const auto& v:it->second.get<picojson::array>())add(v);
  }
  s.torbox_api_key=field(o,"torboxApiKey");
  if(s.torbox_api_key.empty())s.torbox_api_key=field(o,"torbox_api_key");
  s.github_token=field(o,"githubToken");
  if(s.github_token.empty())s.github_token=field(o,"github_token");
  auto r=field(o,"githubRepo");if(r.empty())r=field(o,"github_repo");
  if(!r.empty()&&r.find(' ')==std::string::npos&&r.find("..")==std::string::npos&&r.find('/')!=std::string::npos)s.github_repo=r;
  auto b=field(o,"githubBranch");if(b.empty())b=field(o,"github_branch");
  if(!b.empty()&&b.find(' ')==std::string::npos&&b.find("..")==std::string::npos)s.github_branch=b;
  s.diagnostic_id=field(o,"diagnosticId");
  auto mode=field(o,"diagnosticType");
  if(mode=="movie"||mode=="series")s.diagnostic_type=mode;
  auto tid=o.find("torboxTorrentId");
  if(tid!=o.end()&&tid->second.is<double>()&&tid->second.get<double>()>=0)
    s.torbox_torrent_id=(int)tid->second.get<double>();
  auto fid=o.find("torboxFileId");
  if(fid!=o.end()&&fid->second.is<double>()&&fid->second.get<double>()>=0)
    s.torbox_file_id=(int)fid->second.get<double>();
  auto a=o.find("englishSubtitles");if(a!=o.end()&&a->second.is<bool>())s.subtitles_english=a->second.get<bool>();
  a=o.find("englishAudio");if(a!=o.end()&&a->second.is<bool>())s.audio_english=a->second.get<bool>();
  output=s;return true;
}
bool parse_manifest(const std::string& json,Manifest& out,std::string& error){
  picojson::object o;if(!parse_object(json,o,error))return false;
  Manifest result;result.id=field(o,"id");result.name=field(o,"name");
  if(result.id.empty()||result.name.empty()){error="Addon manifest must contain id and name";return false;}
  auto a=o.find("resources");
  if(a!=o.end()&&a->second.is<picojson::array>())
    for(auto& v:a->second.get<picojson::array>()){
      if(v.is<std::string>()&&v.get<std::string>()=="stream")result.streams=true;
      if(v.is<picojson::object>()&&field(v.get<picojson::object>(),"name")=="stream")result.streams=true;
    }
  auto t=o.find("types");
  if(t!=o.end()&&t->second.is<picojson::array>())
    for(auto& v:t->second.get<picojson::array>())if(v.is<std::string>())result.types.push_back(v.get<std::string>());
  out=result;return true;
}
std::string stream_url(const std::string& manifest_url,const std::string& type,const std::string& id){
  std::string normalized;
  if(!normalize_manifest_url(manifest_url,normalized)||!allowed(type)||!allowed(id))return "";
  return normalized.substr(0,normalized.size()-14)+"/stream/"+enc(type)+"/"+enc(id)+".json";
}
bool Stream::is_playable()const {
  return url.find("https://")==0||url.find("http://")==0;
}
std::vector<Stream> parse_streams(const std::string& json,const std::string& addon,std::string& error){
  std::vector<Stream> result;picojson::object o;
  if(!parse_object(json,o,error))return result;
  auto i=o.find("streams");if(i==o.end()||!i->second.is<picojson::array>()){error="Missing streams array";return result;}
  for(auto& v:i->second.get<picojson::array>()){
    if(!v.is<picojson::object>())continue;
    const auto& st=v.get<picojson::object>();
    Stream x;x.addon=addon;x.name=field(st,"name");x.title=field(st,"title");
    x.url=field(st,"url");x.info_hash=field(st,"infoHash");
    auto fi=st.find("fileIdx");
    if(fi!=st.end()&&fi->second.is<double>())x.file_index=(int)fi->second.get<double>();
    if(x.is_playable()||x.info_hash.size()==40)result.push_back(x);
  }
  return result;
}
std::string torbox_request_url(int torrent_id,int file_id){
  if(torrent_id<0||file_id<0)return "";
  return "https://api.torbox.app/v1/api/torrents/requestdl?torrent_id="+std::to_string(torrent_id)+"&file_id="+std::to_string(file_id);
}
std::string torbox_extract_link(const std::string& json,std::string& error){
  picojson::object o;if(!parse_object(json,o,error))return "";
  auto s=o.find("success");if(s==o.end()||!s->second.is<bool>()||!s->second.get<bool>()){
    error=field(o,"detail");if(error.empty())error="TorBox rejected download request";return "";
  }
  auto url=field(o,"data");if(url.find("https://")!=0){error="TorBox did not return a secure download URL";return "";}
  return url;
}
std::string github_get_url(const Settings& s){
  if(s.github_repo.find('/')==std::string::npos)return "";
  return "https://api.github.com/repos/"+s.github_repo+"/contents/playtorrio/watchlist-v1.json?ref="+enc(s.github_branch);
}
std::string github_put_url(const Settings& s){
  if(s.github_repo.find('/')==std::string::npos)return "";
  return "https://api.github.com/repos/"+s.github_repo+"/contents/playtorrio/watchlist-v1.json";
}
bool parse_library(const std::string& json,std::map<std::string,LibraryRecord>& records,std::string& error){
  picojson::object o;if(!parse_object(json,o,error))return false;
  auto ver=o.find("version"),it=o.find("records");
  if(ver==o.end()||!ver->second.is<double>()||ver->second.get<double>()!=1||it==o.end()||!it->second.is<picojson::object>()){
    error="Incompatible watchlist database; remote data preserved";return false;
  }
  std::map<std::string,LibraryRecord> next;
  for(const auto& entry:it->second.get<picojson::object>()){
    if(!entry.second.is<picojson::object>())continue;
    const auto& v=entry.second.get<picojson::object>();
    auto item=v.find("item");if(item==v.end())continue;
    LibraryRecord record;record.key=entry.first;record.item_json=item->second.serialize();
    auto deleted=v.find("deleted");record.deleted=deleted!=v.end()&&deleted->second.is<bool>()&&deleted->second.get<bool>();
    next[record.key]=record;
  }
  records.swap(next);return true;
}
std::string encode_library(const std::map<std::string,LibraryRecord>& records){
  picojson::object db,entries;db["version"]=picojson::value(1.0);
  for(const auto& e:records){
    picojson::object node;node["deleted"]=picojson::value(e.second.deleted);
    picojson::value val;auto err=picojson::parse(val,e.second.item_json);
    if(!err.empty())continue;
    node["item"]=val;entries[e.first]=picojson::value(node);
  }
  db["records"]=picojson::value(entries);
  return picojson::value(db).serialize();
}
std::map<std::string,LibraryRecord> merge_library(
    const std::map<std::string,LibraryRecord>& remote,
    const std::map<std::string,LibraryRecord>& pending){
  auto merged=remote;for(auto& p:pending)merged[p.first]=p.second;return merged;
}
std::string base64_encode(const std::string& in){
  static const char* table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;int val=0,bits=-6;
  for(unsigned char c:in){val=(val<<8)+c;bits+=8;while(bits>=0){out+=table[(val>>bits)&63];bits-=6;}}
  if(bits>-6)out+=table[((val<<8)>>(bits+8))&63];
  while(out.size()%4)out+='=';
  return out;
}
std::string base64_decode(const std::string& in){
  static const std::string table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;int val=0,bits=-8;
  for(unsigned char c:in){
    if(c=='='||c==0)break;
    auto n=table.find(c);
    if(n==std::string::npos){if(std::isspace(c))continue;return "";}
    val=(val<<6)+(int)n;bits+=6;if(bits>=0){out+=(char)((val>>bits)&255);bits-=8;}
  }
  return out;
}
std::string github_contents_update(const std::string& json,const std::string& sha,const std::string& branch){
  picojson::object o;
  o["message"]=picojson::value("library: sync watchlist from PlayTorrio PS4");
  o["sha"]=picojson::value(sha);
  o["branch"]=picojson::value(branch);
  o["content"]=picojson::value(base64_encode(json));
  return picojson::value(o).serialize();
}
}
