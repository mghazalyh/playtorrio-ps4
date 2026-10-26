#pragma once
// Private-to-device saved list; title/public TMDB metadata ONLY.
// Never used for cloud GitHub or signed stream URL storage.
#include "catalog.hpp"
#include "../vendor/picojson.h"
#include <vector>
#include <string>
#include <algorithm>
namespace ptvlocal {
inline int id(const picojson::object& r){
 auto p=r.find("tmdb_id");if(p==r.end()||!p->second.is<double>())return 0;
 const double val=p->second.get<double>();
 return val>0&&val<=999999999&&val==(int)val?(int)val:0;
}
inline std::string field(const picojson::object& r,const char* k){
 auto p=r.find(k);return p!=r.end()&&p->second.is<std::string>()?p->second.get<std::string>():"";
}
inline bool decode(const std::string& data,std::vector<ptvcatalog::Item>& saved){
 picojson::value doc;
 if(data.size()>128*1024||!picojson::parse(doc,data).empty()||
    !doc.is<picojson::object>())return false;
 const auto& o=doc.get<picojson::object>();
 auto version=o.find("version");
 auto rows=o.find("saved");
 if(version==o.end()||!version->second.is<double>()||
    version->second.get<double>()!=1||rows==o.end()||
    !rows->second.is<picojson::array>())return false;
 std::vector<ptvcatalog::Item> parsed;
 for(const auto& v:rows->second.get<picojson::array>()){
  if(!v.is<picojson::object>()||parsed.size()>=80)continue;
  const auto& j=v.get<picojson::object>();
  ptvcatalog::Item item;item.tmdb_id=id(j);item.title=field(j,"title");
  item.year=field(j,"year");item.overview=field(j,"overview");
  item.poster_path=field(j,"poster");item.backdrop_path=field(j,"backdrop");
  item.imdb_id=field(j,"imdb");
  const auto t=j.find("series");
  item.series=t!=j.end()&&t->second.is<bool>()&&t->second.get<bool>();
  if(item.tmdb_id<=0||item.title.empty()||item.title.size()>140||item.year.size()>8||
    item.overview.size()>600||item.poster_path.size()>140||item.imdb_id.size()>25)continue;
  if(std::none_of(parsed.begin(),parsed.end(),[&](const ptvcatalog::Item& x){
    return x.tmdb_id==item.tmdb_id&&x.series==item.series;
  }))parsed.push_back(std::move(item));
 }
 saved.swap(parsed);return true;
}
inline std::string encode(const std::vector<ptvcatalog::Item>& saved){
 picojson::array rows;
 for(const auto& item:saved){
  if(rows.size()>=80)break;
  picojson::object row;
  row["tmdb_id"]=picojson::value((double)item.tmdb_id);
  row["title"]=picojson::value(item.title);
  row["year"]=picojson::value(item.year);
  row["overview"]=picojson::value(item.overview);
  row["poster"]=picojson::value(item.poster_path);
  row["backdrop"]=picojson::value(item.backdrop_path);
  row["imdb"]=picojson::value(item.imdb_id);
  row["series"]=picojson::value(item.series);
  rows.emplace_back(row);
 }
 picojson::object root;
 root["version"]=picojson::value(1.0);
 root["saved"]=picojson::value(rows);
 return picojson::value(root).serialize();
}
inline bool includes(const std::vector<ptvcatalog::Item>& saved,const ptvcatalog::Item& item){
 return std::any_of(saved.begin(),saved.end(),[&](const ptvcatalog::Item& x){
  return x.tmdb_id==item.tmdb_id&&x.series==item.series;
 });
}
inline bool toggle(std::vector<ptvcatalog::Item>& saved,const ptvcatalog::Item& item){
 auto it=std::find_if(saved.begin(),saved.end(),[&](const ptvcatalog::Item& x){
  return x.tmdb_id==item.tmdb_id&&x.series==item.series;
 });
 if(it!=saved.end()){saved.erase(it);return false;}
 saved.insert(saved.begin(),item);
 if(saved.size()>80)saved.resize(80);
 return true;
}
}
