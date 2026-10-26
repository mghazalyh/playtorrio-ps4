#include "../src/local_library.hpp"
#include <cassert>
#include <cstdio>
int main(){
 ptvcatalog::Item movie;movie.tmdb_id=123;movie.title="Original movie";
 movie.poster_path="/poster.jpg";movie.year="2026";movie.series=false;
 ptvcatalog::Item show=movie;show.series=true;
 std::vector<ptvcatalog::Item> list;
 assert(ptvlocal::toggle(list,movie));
 assert(ptvlocal::includes(list,movie));
 assert(!ptvlocal::includes(list,show));
 assert(ptvlocal::toggle(list,show));
 assert(list.size()==2);
 const auto json=ptvlocal::encode(list);
 assert(json.find("token")==std::string::npos);
 std::vector<ptvcatalog::Item> restored;
 assert(ptvlocal::decode(json,restored));
 assert(restored.size()==2);
 assert(ptvlocal::includes(restored,show));
 assert(!ptvlocal::toggle(restored,movie));
 assert(!ptvlocal::includes(restored,movie));
 assert(!ptvlocal::decode("{}",restored));
 assert(restored.size()==1);
 std::puts("Local-only title list, JSON persistence and no-cloud dedupe passed");
}