#include "../src/show_details.hpp"
#include <cassert>
#include <cstdio>
int main(){
  std::vector<ptvdetail::Season> seasons;
  const std::string detail=R"({"seasons":[{"season_number":0,"episode_count":5,"name":"Specials"},{"season_number":2,"episode_count":8,"name":"Season 2"},{"season_number":1,"episode_count":10,"name":"Season 1"}]})";
  assert(ptvdetail::parse_seasons(detail,seasons));
  assert(seasons.size()==2&&seasons[0].number==1&&seasons[1].number==2);
  std::vector<ptvdetail::Episode> eps;
  const std::string body=R"({"episodes":[{"episode_number":2,"name":"Return","overview":"Second"},{"episode_number":1,"name":"Pilot","still_path":"/demo.jpg"},{"episode_number":0,"name":"Trailer"}]})";
  assert(ptvdetail::parse_episodes(body,1,eps));
  assert(eps.size()==2&&eps[0].number==1&&eps[0].title=="Pilot");
  assert(eps[0].still=="/demo.jpg");
  assert(!ptvdetail::parse_seasons("{}",seasons));
  assert(!ptvdetail::parse_episodes("{}",1,eps));
  assert(ptvdetail::stremio_episode_id("tt1234",2,7)=="tt1234:2:7");
  assert(ptvdetail::stremio_episode_id("xx1234",2,7).empty());
  assert(ptvdetail::stremio_episode_id("tt1234",0,0).empty());
  std::puts("TV season/episode browsing and Stremio IDs passed");
}
