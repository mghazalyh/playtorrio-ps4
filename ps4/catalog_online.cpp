#include "catalog_online.hpp"
#include "native_net.hpp"
#include "../vendor/picojson.h"
#include <algorithm>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace ptvcatalog {
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
  // Optional IMDb identifiers for the first six title selections per category.
  auto find_imdb=[&](Item& item){
    const auto response=fetch(std::string(item.series?"/tv/":"/movie/")+
                  std::to_string(item.tmdb_id)+"/external_ids");
    if(response.status!=200||response.platform_error!=0)return;
    picojson::value obj;
    if(!picojson::parse(obj,response.body).empty()||!obj.is<picojson::object>())return;
    const auto& fields=obj.get<picojson::object>();
    auto found=fields.find("imdb_id");
    if(found==fields.end()||!found->second.is<std::string>())return;
    const std::string imdb=found->second.get<std::string>();
    if(imdb.size()<3||imdb.size()>20||imdb.substr(0,2)!="tt")return;
    if(!std::all_of(imdb.begin()+2,imdb.end(),
       [](unsigned char c){return c>='0'&&c<='9';}))return;
    item.imdb_id=imdb;
  };
  for(size_t i=0;i<std::min(size_t(6),catalog.tv.size());i++)find_imdb(catalog.tv[i]);
  for(size_t i=0;i<std::min(size_t(6),catalog.movies.size());i++)find_imdb(catalog.movies[i]);
  catalog.updated="Native TMDB "+std::to_string((long long)std::time(nullptr));
  catalog.loaded=true;
  return catalog;
}
}
