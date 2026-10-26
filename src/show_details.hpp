#pragma once
// Portable TMDB season/episode model for native PS4 and host tests.
#include "../vendor/picojson.h"
#include <algorithm>
#include <string>
#include <vector>

namespace ptvdetail {
struct Season {int number=1,count=0;std::string name,poster;};
struct Episode {int season=1,number=1;std::string title,overview,still;};
inline std::string field(const picojson::object& o,const char* key){
  const auto i=o.find(key);
  return i!=o.end()&&i->second.is<std::string>()?i->second.get<std::string>():"";
}
inline int integer(const picojson::object& o,const char* key,int fallback=0){
  const auto i=o.find(key);
  if(i==o.end()||!i->second.is<double>())return fallback;
  const double v=i->second.get<double>();
  return v>=0&&v<=10000&&v==(int)v?(int)v:fallback;
}
inline bool parse_seasons(const std::string& json,std::vector<Season>& seasons){
  seasons.clear();
  picojson::value root;
  if(!picojson::parse(root,json).empty()||!root.is<picojson::object>())return false;
  const auto& o=root.get<picojson::object>();
  const auto it=o.find("seasons");
  if(it==o.end()||!it->second.is<picojson::array>())return false;
  for(const auto& v:it->second.get<picojson::array>()){
    if(!v.is<picojson::object>()||seasons.size()>=50)continue;
    const auto& x=v.get<picojson::object>();
    Season s;
    s.number=integer(x,"season_number",-1);
    s.count=integer(x,"episode_count",0);
    s.name=field(x,"name");
    s.poster=field(x,"poster_path");
    if(s.number<1||s.count<1||s.number>500)continue;
    if(s.name.empty())s.name="Season "+std::to_string(s.number);
    seasons.push_back(std::move(s));
  }
  std::sort(seasons.begin(),seasons.end(),[](const Season& a,const Season& b){return a.number<b.number;});
  return true;
}
inline bool parse_episodes(const std::string& json,int season,std::vector<Episode>& episodes){
  episodes.clear();
  if(season<1||season>500)return false;
  picojson::value root;
  if(!picojson::parse(root,json).empty()||!root.is<picojson::object>())return false;
  const auto& o=root.get<picojson::object>();
  const auto it=o.find("episodes");
  if(it==o.end()||!it->second.is<picojson::array>())return false;
  for(const auto& v:it->second.get<picojson::array>()){
    if(!v.is<picojson::object>()||episodes.size()>=100)continue;
    const auto& e=v.get<picojson::object>();
    Episode ep;ep.season=season;ep.number=integer(e,"episode_number",0);
    if(ep.number<1||ep.number>2000)continue;
    ep.title=field(e,"name");ep.overview=field(e,"overview");ep.still=field(e,"still_path");
    if(ep.title.empty())ep.title="Episode "+std::to_string(ep.number);
    episodes.push_back(std::move(ep));
  }
  std::sort(episodes.begin(),episodes.end(),[](const Episode& a,const Episode& b){return a.number<b.number;});
  return true;
}
inline std::string stremio_episode_id(const std::string& imdb,int season,int episode){
  if(imdb.size()<4||imdb.substr(0,2)!="tt"||season<1||episode<1||season>500||episode>2000)return {};
  if(!std::all_of(imdb.begin()+2,imdb.end(),[](unsigned char c){return c>='0'&&c<='9';}))return {};
  return imdb+":"+std::to_string(season)+":"+std::to_string(episode);
}
}
