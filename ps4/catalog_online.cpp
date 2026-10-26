#include "catalog_online.hpp"
#include "native_net.hpp"
#include "../vendor/picojson.h"
#include <algorithm>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace ptvcatalog {
static ps4net::Response api(const ptv::Settings& cfg,const std::string& endpoint){
  std::map<std::string,std::string> headers={{"Accept","application/json"}};
  const bool bearer=cfg.tmdb_api_key.rfind("eyJ",0)==0;
  if(bearer)headers["Authorization"]="Bearer "+cfg.tmdb_api_key;
  std::string url="https://api.themoviedb.org/3"+endpoint+
                   (endpoint.find('?')==std::string::npos?"?":"&")+"language=en-US";
  if(!bearer)url+="&api_key="+cfg.tmdb_api_key;
  return ps4net::request(url,"GET",headers,"",256*1024);
}
bool fetch_seasons(const ptv::Settings& cfg,int tmdbId,
                   std::vector<ptvdetail::Season>& result){
  if(cfg.tmdb_api_key.empty()||tmdbId<=0)return false;
  auto res=api(cfg,"/tv/"+std::to_string(tmdbId));
  return res.status==200&&!res.platform_error&&!res.truncated&&
         ptvdetail::parse_seasons(res.body,result);
}
bool fetch_episodes(const ptv::Settings& cfg,int tmdbId,int season,
                    std::vector<ptvdetail::Episode>& result){
  if(cfg.tmdb_api_key.empty()||tmdbId<=0||season<=0||season>500)return false;
  auto res=api(cfg,"/tv/"+std::to_string(tmdbId)+"/season/"+std::to_string(season));
  return res.status==200&&!res.platform_error&&!res.truncated&&
         ptvdetail::parse_episodes(res.body,season,result);
}
static std::string failure(const char* step,const ps4net::Response& r){
  return std::string(step)+" http="+std::to_string(r.status)+
    " transport="+std::to_string(r.platform_error)+
    " errno="+std::to_string(r.last_errno);
}
static void append(const std::vector<Item>& from,std::vector<Item>& into){
  for(const auto& item:from){
    if(into.size()>=40)break;
    if(std::none_of(into.begin(),into.end(),
      [&](const Item& other){return item.tmdb_id==other.tmdb_id;}))
      into.push_back(item);
  }
}
std::string imdb_for_tmdb(const ptv::Settings& cfg,bool series,int tmdbId){
  if(tmdbId<=0||cfg.tmdb_api_key.empty())return {};
  const bool bearer=cfg.tmdb_api_key.rfind("eyJ",0)==0;
  std::map<std::string,std::string> headers={{"Accept","application/json"}};
  std::string url="https://api.themoviedb.org/3/"+
                  std::string(series?"tv/":"movie/")+
                  std::to_string(tmdbId)+"/external_ids";
  if(bearer)headers["Authorization"]="Bearer "+cfg.tmdb_api_key;
  else url+="?api_key="+cfg.tmdb_api_key;
  const auto response=ps4net::request(url,"GET",headers,"",96*1024);
  if(response.platform_error||response.truncated||response.status!=200)return {};
  picojson::value obj;
  if(!picojson::parse(obj,response.body).empty()||!obj.is<picojson::object>())return {};
  const auto& fields=obj.get<picojson::object>();
  auto found=fields.find("imdb_id");
  if(found==fields.end()||!found->second.is<std::string>())return {};
  const std::string imdb=found->second.get<std::string>();
  if(imdb.size()<3||imdb.size()>20||imdb.substr(0,2)!="tt")return {};
  if(!std::all_of(imdb.begin()+2,imdb.end(),
     [](unsigned char c){return c>='0'&&c<='9';}))return {};
  return imdb;
}
std::vector<Item> fetch_related(const ptv::Settings& cfg,bool series,int tmdbId){
  std::vector<Item> results;
  if(tmdbId<=0||cfg.tmdb_api_key.empty())return results;
  const std::string path=std::string(series?"/tv/":"/movie/")+std::to_string(tmdbId);
  for(const std::string kind:{"recommendations","similar"}){
    const auto res=api(cfg,path+"/"+kind+"?page=1");
    if(res.platform_error||res.truncated||res.status!=200)continue;
    std::vector<Item> from;
    std::string error;
    if(!parse_tmdb_results(res.body,series,from,error))continue;
    for(auto& item:from){
      if(item.tmdb_id==tmdbId)continue;
      if(std::none_of(results.begin(),results.end(),[&](const Item& old){return old.tmdb_id==item.tmdb_id;}))
        results.push_back(std::move(item));
      if(results.size()>=12)return results;
    }
    if(results.size()>=6)break;
  }
  return results;
}
Catalog fetch_online(const ptv::Settings& cfg){
  Catalog catalog;
  if(cfg.tmdb_api_key.empty()){catalog.error="No TMDB API key";return catalog;}
  const bool bearer=cfg.tmdb_api_key.rfind("eyJ",0)==0;
  std::map<std::string,std::string> headers={{"Accept","application/json"}};
  if(bearer)headers["Authorization"]="Bearer "+cfg.tmdb_api_key;
  auto fetch=[&](const std::string& path){
    std::string url="https://api.themoviedb.org/3"+path+
      (path.find('?')==std::string::npos?"?":"&")+"language=en-US";
    if(!bearer)url+="&api_key="+cfg.tmdb_api_key;
    return ps4net::request(url,"GET",headers,"",512*1024);
  };
  struct Endpoint {const char* path;bool tv;const char* name;};
  const Endpoint endpoints[]={
    {"/trending/tv/week",true,"TV trending"},
    {"/trending/movie/week",false,"Movie trending"},
    {"/tv/popular",true,"TV popular"},
    {"/movie/popular",false,"Movie popular"}
  };
  for(const auto& endpoint:endpoints){
    const auto response=fetch(endpoint.path);
    if(response.status!=200||response.platform_error!=0){
      if(catalog.tv.empty()||catalog.movies.empty()){
        catalog.error=failure(endpoint.name,response);return catalog;
      }
      continue;
    }
    std::vector<Item> items;
    std::string error;
    if(!parse_tmdb_results(response.body,endpoint.tv,items,error)){
      if(catalog.tv.empty()||catalog.movies.empty()){
        catalog.error=std::string(endpoint.name)+": "+error;return catalog;
      }
      continue;
    }
    append(items,endpoint.tv?catalog.tv:catalog.movies);
  }
  if(catalog.tv.empty()||catalog.movies.empty()){
    catalog.error="Missing TV or movie results";return catalog;
  }
  // Prefetch the initial screen; later pages resolve IMDb IDs on demand.
  auto find_imdb=[&](Item& item){
    item.imdb_id=imdb_for_tmdb(cfg,item.series,item.tmdb_id);
  };
  for(size_t i=0;i<std::min(size_t(6),catalog.tv.size());i++)find_imdb(catalog.tv[i]);
  for(size_t i=0;i<std::min(size_t(6),catalog.movies.size());i++)find_imdb(catalog.movies[i]);
  catalog.updated="Native TMDB "+std::to_string((long long)std::time(nullptr));
  catalog.loaded=true;
  return catalog;
}
}
