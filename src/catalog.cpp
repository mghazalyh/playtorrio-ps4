#include "catalog.hpp"
#include "../vendor/picojson.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace ptvcatalog {
static std::string get(const picojson::object& o,const char* field){
  const auto i=o.find(field);
  return i!=o.end()&&i->second.is<std::string>()?i->second.get<std::string>():"";
}
static int getInt(const picojson::object& o,const char* field) {
  auto i=o.find(field);
  if(i==o.end()||!i->second.is<double>())return 0;
  auto v=i->second.get<double>();
  return v>=1&&v<=999999999&&(int)v==v?(int)v:0;
}
static std::string safeText(const std::string& in,size_t maxChars) {
  // Current OpenOrbis FreeType renderer treats one input byte as one glyph.
  // Keep English/ASCII copy here until a UTF-8 shaper is integrated.
  std::string out;
  for(unsigned char c:in) {
    if(out.size()>=maxChars)break;
    if(c>=32&&c<=126)out.push_back(c);
    else if(c=='\n'||c=='\t'||c>=128)out.push_back(' ');
  }
  return out;
}
static void section(const picojson::object& o,const char* key,bool series,
                    std::vector<Item>& out) {
  auto it=o.find(key);
  if(it==o.end()||!it->second.is<picojson::array>())return;
  for(const auto& v:it->second.get<picojson::array>()){
    if(out.size()>=40)break;
    if(!v.is<picojson::object>())continue;
    const auto& obj=v.get<picojson::object>();
    Item entry;
    entry.tmdb_id=getInt(obj,"id");
    entry.title=safeText(get(obj,"title"),70);
    entry.year=safeText(get(obj,"year"),4);
    entry.overview=safeText(get(obj,"overview"),270);
    entry.imdb_id=get(obj,"imdbId");
    entry.poster_path=get(obj,"poster_path");
    entry.backdrop_path=get(obj,"backdrop_path");
    if(entry.imdb_id.size()>20||entry.imdb_id.rfind("tt",0)!=0 ||
        !std::all_of(entry.imdb_id.begin()+2,entry.imdb_id.end(),
             [](unsigned char c){return c>='0'&&c<='9';}))
       entry.imdb_id.clear();
    auto r=obj.find("rating");
    if(r!=obj.end()&&r->second.is<double>()&&std::isfinite(r->second.get<double>()))
      entry.rating=std::max(0.f,std::min(10.f,(float)r->second.get<double>()));
    entry.series=series;
    if(entry.tmdb_id>0&&!entry.title.empty())out.push_back(entry);
  }
}
bool parse_catalog(const std::string& src,Catalog& result,std::string& error) {
  picojson::value root;
  error=picojson::parse(root,src);
  if(!error.empty()||!root.is<picojson::object>()){error="Invalid catalog JSON";return false;}
  const auto& o=root.get<picojson::object>();
  if(getInt(o,"version")!=1){error="Unsupported catalog format";return false;}
  Catalog catalog;
  catalog.updated=safeText(get(o,"updated"),40);
  section(o,"tv",true,catalog.tv);
  section(o,"movies",false,catalog.movies);
  if(catalog.tv.empty()&&catalog.movies.empty()){error="No TMDB titles in catalog";return false;}
  catalog.loaded=true;
  result=std::move(catalog);
  return true;
}
bool parse_tmdb_results(const std::string& body,bool tv,std::vector<Item>& items,std::string& error){
  picojson::value doc;
  error=picojson::parse(doc,body);
  if(!error.empty()||!doc.is<picojson::object>()){error="Invalid TMDB JSON";return false;}
  const auto& o=doc.get<picojson::object>();
  auto found=o.find("results");
  if(found==o.end()||!found->second.is<picojson::array>()){error="TMDB results missing";return false;}
  std::vector<Item> parsed;
  for(const auto& row:found->second.get<picojson::array>()){
    if(parsed.size()>=40)break;
    if(!row.is<picojson::object>())continue;
    const auto& r=row.get<picojson::object>();
    Item item;
    item.tmdb_id=getInt(r,"id");
    item.title=safeText(get(r,tv?"name":"title"),70);
    item.overview=safeText(get(r,"overview"),270);
    item.poster_path=get(r,"poster_path");
    item.backdrop_path=get(r,"backdrop_path");
    const auto date=get(r,tv?"first_air_date":"release_date");
    item.year=date.size()>=4?safeText(date.substr(0,4),4):std::string{};
    item.series=tv;
    auto score=r.find("vote_average");
    if(score!=r.end()&&score->second.is<double>()&&std::isfinite(score->second.get<double>()))
      item.rating=std::max(0.f,std::min(10.f,(float)score->second.get<double>()));
    if(item.tmdb_id<=0||item.title.empty())continue;
    if(std::none_of(parsed.begin(),parsed.end(),[&](const Item& old){return old.tmdb_id==item.tmdb_id;}))
      parsed.push_back(std::move(item));
  }
  if(parsed.empty()){error="TMDB returned no valid titles";return false;}
  items=std::move(parsed);error.clear();return true;
}
Catalog load_file(const char* path){
  Catalog out;
  FILE* f=std::fopen(path,"rb");
  if(!f){out.error="Catalog not generated. Run HostTools/Load-Shows.cmd on Windows.";return out;}
  std::string text;
  char buf[4096];
  while(text.size()<256*1024){
    const size_t n=std::fread(buf,1,std::min(sizeof(buf),256*1024-text.size()),f);
    if(n)text.append(buf,n);
    if(n<sizeof(buf))break;
  }
  const bool failure=std::ferror(f)!=0;
  std::fclose(f);
  if(failure){out.error="Cannot read local catalog";return out;}
  if(text.size()>=256*1024){out.error="Catalog JSON exceeds 256 KiB";return out;}
  if(text.size()>=3 && (unsigned char)text[0]==0xef &&
     (unsigned char)text[1]==0xbb && (unsigned char)text[2]==0xbf)
     text.erase(0,3);
  std::string reason;
  if(!parse_catalog(text,out,reason))out.error=reason;
  return out;
}
}
