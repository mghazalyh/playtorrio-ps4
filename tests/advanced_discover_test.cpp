#include "../src/advanced_discover.hpp"
#include <cassert>
#include <cstdio>
using namespace ptvdiscover;
int main(){
 Filters f;
 assert(query(f).find("/discover/tv?")==0);
 assert(query(f).find("sort_by=popularity.desc")!=std::string::npos);
 f.from_year=2022;f.to_year=2026;
 assert(query(f).find("air_date.gte=2022-01-01")!=std::string::npos);
 f.country="JP";f.language="ja";
 assert(query(f).find("with_origin_country=JP")!=std::string::npos);
 assert(query(f).find("with_original_language=ja")!=std::string::npos);
 f.series=false;f.sort="vote_average.desc";
 assert(query(f).find("primary_release_date.gte=2022-01-01")!=std::string::npos);
 assert(query(f).find("vote_count.gte=100")!=std::string::npos);
 f.title="The Order";f.from_year=2026;f.to_year=2026;
 assert(query(f).find("/search/movie?")==0);
 assert(query(f).find("query=The%20Order")!=std::string::npos);
 assert(query(f).find("primary_release_year=2026")!=std::string::npos);
 assert(query(f).find("with_origin_country")==std::string::npos);
 f.series=true;
 assert(query(f).find("&year=2026")!=std::string::npos);
 f.from_year=2022;
 assert(query(f).find("&year=")==std::string::npos);
 picojson::object row;row["original_language"]=picojson::value("ja");
 row["first_air_date"]=picojson::value("2024-01-01");
 row["vote_count"]=picojson::value(250.0);
 row["popularity"]=picojson::value(99.0);
 row["vote_average"]=picojson::value(8.0);
 picojson::array origin;origin.emplace_back("JP");
 row["origin_country"]=picojson::value(origin);
 assert(matches(row,f));
 f.from_year=2025;f.to_year=2026;assert(!matches(row,f));
 f.from_year=2026;f.to_year=2026;assert(matches(row,f)); // aired during year
 f.language="en";assert(!matches(row,f));
 f.language="ja";f.country="US";assert(!matches(row,f));
 f.country="JP";f.sort="vote_average.desc";assert(matches(row,f));
 row["vote_count"]=picojson::value(30.0);assert(!matches(row,f));
 f.from_year=2030;f.to_year=2020;assert(!valid(f));
 f.from_year=0;f.to_year=0;f.page=501;assert(!valid(f));
 std::puts("Active ptv3 v1.1.7 Discover filter parity regression passed");
}