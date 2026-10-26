#include "advanced_discover.hpp"
#include "native_net.hpp"
#include <map>
#include <vector>
#include <algorithm>
#include <orbis/libkernel.h>

namespace ptvdiscover {
namespace {
ps4net::Response tmdb(const ptv::Settings& cfg,const std::string& path,size_t limit=512*1024){
 const bool bearer=cfg.tmdb_api_key.rfind("eyJ",0)==0;
 std::map<std::string,std::string> headers={{"Accept","application/json"}};
 if(bearer)headers["Authorization"]="Bearer "+cfg.tmdb_api_key;
 std::string url="https://api.themoviedb.org/3"+path;
 url+=path.find('?')==std::string::npos?"?":"&";
 if(!bearer)url+="api_key="+escaped(cfg.tmdb_api_key);
 else url.pop_back();
 return ps4net::request(url,"GET",headers,"",limit);
}
}
Page fetch(const ptv::Settings& cfg,const Filters& filter){
 Page result;result.number=filter.page;
 if(cfg.tmdb_api_key.empty()||!valid(filter)){result.error="Invalid TMDB key or filters";return result;}
 const std::string path=query(filter);
 const auto response=tmdb(cfg,path);
 if(response.status!=200||response.platform_error||response.truncated){
  result.error="TMDB browse HTTP "+std::to_string(response.status)+
    " transport "+std::to_string(response.platform_error);return result;
 }
 picojson::value doc;
 if(!picojson::parse(doc,response.body).empty()||!doc.is<picojson::object>()){
  result.error="Invalid TMDB browse JSON";return result;
 }
 const auto& obj=doc.get<picojson::object>();
 result.number=number(obj,"page");
 result.total_pages=std::max(1,std::min(500,number(obj,"total_pages")));
 result.raw_total=number(obj,"total_results");
 auto it=obj.find("results");
 if(it==obj.end()||!it->second.is<picojson::array>()){
  result.error="No result list from TMDB";return result;
 }
 picojson::array rows;
 const bool searched=!filter.title.empty()&&filter.title.find_first_not_of(" \t\n\r")!=std::string::npos;
 for(const auto& candidate:it->second.get<picojson::array>()){
  if(!candidate.is<picojson::object>())continue;
  auto row=candidate.get<picojson::object>();
  if(searched){
   Filters withoutCountry=filter;withoutCountry.country.clear();
   if(!matches(row,withoutCountry))continue;
   if(!filter.country.empty()&&!has_country(row,filter.country)){
    if(filter.series)continue;
    // Only for text+movie+country (as in ptv3). Up to one cached/paced
    // movie details request per still-eligible result, never for blank text.
    const int movieId=number(row,"id");
    if(movieId<=0)continue;
    sceKernelUsleep(350000);
    const auto details=tmdb(cfg,"/movie/"+std::to_string(movieId)+"?language=en-US",128*1024);
    picojson::value movie;
    if(details.status!=200||details.platform_error||details.truncated||
       !picojson::parse(movie,details.body).empty()||!movie.is<picojson::object>())continue;
    if(!matches(row,filter,&movie.get<picojson::object>()))continue;
   }
  }
  rows.emplace_back(std::move(candidate));
 }
 if(searched)sort_results(rows,filter);
 picojson::object clean;
 clean["results"]=picojson::value(std::move(rows));
 const std::string body=picojson::value(clean).serialize();
 std::string error;
 if(!ptvcatalog::parse_tmdb_results(body,filter.series,result.titles,error)){
  // An empty filtered page is valid; next page might contain matches.
  if(error!="TMDB returned no valid titles"){result.error=error;return result;}
 }
 result.loaded=true;
 return result;
}
}
