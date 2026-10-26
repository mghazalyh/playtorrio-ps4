#pragma once
// PS4-portable version of ptv3's active v1.1.7 DiscoverFilters semantics.
// Blank query uses TMDB discover; title query uses TMDB search then client filters.
#include "../vendor/picojson.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ptvdiscover {
struct Filters {
 bool series=true;
 std::string title,country,language,sort="popularity.desc";
 int from_year=0,to_year=0,page=1;
};
inline std::string escaped(const std::string& s) {
 static const char digits[]="0123456789ABCDEF";
 std::string out;
 for(unsigned char c:s) {
   if(std::isalnum(c)||c=='_'||c=='-'||c=='.'||c=='~')out+=(char)c;
   else {out+='%';out+=digits[c>>4];out+=digits[c&15];}
 }
 return out;
}
inline bool valid(const Filters& f){
 return f.page>=1&&f.page<=500&&f.title.size()<=100&&f.country.size()<=3&&
  f.language.size()<=3&&(f.from_year==0||(f.from_year>=1900&&f.from_year<=2100))&&
  (f.to_year==0||(f.to_year>=1900&&f.to_year<=2100))&&
  (!f.from_year||!f.to_year||f.from_year<=f.to_year)&&
  (f.sort=="popularity.desc"||f.sort=="vote_average.desc"||f.sort=="vote_count.desc");
}
inline std::string query(const Filters& f){
 if(!valid(f))return {};
 std::string title=f.title;
 const auto first=title.find_first_not_of(" \r\n\t");
 if(first==std::string::npos)title.clear();
 else{const auto last=title.find_last_not_of(" \r\n\t");title=title.substr(first,last-first+1);}
 const bool searched=!title.empty();
 std::string url=std::string(searched?"/search/":"/discover/")+(f.series?"tv":"movie")+
  "?page="+std::to_string(f.page)+"&language=en-US&include_adult=false";
 const int firstYear=f.from_year?f.from_year:f.to_year;
 const int lastYear=f.to_year?f.to_year:f.from_year;
 if(searched){
   url+="&query="+escaped(title);
   // Single-year TV search: TMDB year includes any episode airing that year.
   if(firstYear&&firstYear==lastYear)
     url+=std::string(f.series?"&year=":"&primary_release_year=")+std::to_string(firstYear);
 }else{
   url+="&sort_by="+escaped(f.sort);
   if(f.sort=="vote_average.desc")url+="&vote_count.gte=100";
   if(firstYear&&lastYear){
     if(f.series)url+="&air_date.gte="+std::to_string(firstYear)+"-01-01&air_date.lte="+std::to_string(lastYear)+"-12-31";
     else url+="&primary_release_date.gte="+std::to_string(firstYear)+"-01-01&primary_release_date.lte="+std::to_string(lastYear)+"-12-31";
   }
   if(!f.country.empty())url+="&with_origin_country="+escaped(f.country);
   if(!f.language.empty())url+="&with_original_language="+escaped(f.language);
 }
 return url;
}
inline int number(const picojson::object& obj,const char* key){
 auto it=obj.find(key);return it!=obj.end()&&it->second.is<double>()?(int)it->second.get<double>():0;
}
inline std::string str(const picojson::object& obj,const char* key){
 auto it=obj.find(key);return it!=obj.end()&&it->second.is<std::string>()?
   it->second.get<std::string>():"";
}
inline bool has_country(const picojson::object& row,const std::string& country){
 auto it=row.find("origin_country");
 if(it!=row.end()&&it->second.is<picojson::array>())
   for(const auto& countryValue:it->second.get<picojson::array>())
     if(countryValue.is<std::string>()&&countryValue.get<std::string>()==country)return true;
 it=row.find("production_countries");
 if(it!=row.end()&&it->second.is<picojson::array>())
   for(const auto& p:it->second.get<picojson::array>())
     if(p.is<picojson::object>()&&str(p.get<picojson::object>(),"iso_3166_1")==country)return true;
 return false;
}
inline bool matches(const picojson::object& row,const Filters& f,
                    const picojson::object* movieDetails=nullptr){
 if(!f.language.empty()&&str(row,"original_language")!=f.language)return false;
 if(f.sort=="vote_average.desc"&&number(row,"vote_count")<100)return false;
 const int first=f.from_year?f.from_year:f.to_year;
 const int last=f.to_year?f.to_year:f.from_year;
 if(first&&last&&!(f.series&&first==last)){
   const auto date=str(row,f.series?"first_air_date":"release_date");
   if(date.size()<4)return false;
   int year=0;for(int i=0;i<4;i++){if(!std::isdigit((unsigned char)date[i]))return false;year=year*10+date[i]-'0';}
   if(year<first||year>last)return false;
 }
 if(!f.country.empty()&&!has_country(row,f.country)&&
    (!movieDetails||!has_country(*movieDetails,f.country)))return false;
 return true;
}
inline double sort_value(const picojson::object& row,const Filters& f){
 const char* key=f.sort=="vote_average.desc"?"vote_average":
                 f.sort=="vote_count.desc"?"vote_count":"popularity";
 auto i=row.find(key);return i!=row.end()&&i->second.is<double>()?i->second.get<double>():0;
}
inline void sort_results(picojson::array& rows,const Filters& f){
 std::stable_sort(rows.begin(),rows.end(),[&](const picojson::value& a,const picojson::value& b){
  if(!a.is<picojson::object>()||!b.is<picojson::object>())return false;
  return sort_value(a.get<picojson::object>(),f)>sort_value(b.get<picojson::object>(),f);
 });
}
}
