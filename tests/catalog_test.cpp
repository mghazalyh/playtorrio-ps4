#include "../src/catalog.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <unistd.h>
using namespace ptvcatalog;
int main(){
  const std::string json=R"json({"version":1,"updated":"2026-10-27 00:00 UTC","tv":[{"id":101,"title":"Real Series","year":"2026","rating":8.6,"overview":"A TV series","imdbId":"tt1234567"},{"id":0,"title":"Invalid"},{"id":102,"title":"No IMDb ID","imdbId":"bad"},{"id":103,"title":"Episode Two","imdbId":"tt3217654"}],"movies":[{"id":201,"title":"Movie One","year":"2025","rating":7.5,"imdbId":"tt2345678"}]})json";
  std::string error; Catalog result;
  assert(parse_catalog(json,result,error));
  assert(result.loaded&&result.tv.size()==3&&result.movies.size()==1);
  assert(result.tv[0].series&&result.tv[0].imdb_id=="tt1234567");
  assert(result.tv[1].imdb_id.empty());
  assert(!result.movies[0].series&&result.movies[0].tmdb_id==201);
  Catalog previous=result;
  assert(!parse_catalog(R"({"version":2,"tv":[]})",result,error));
  assert(result.tv.size()==previous.tv.size());
  char file[]="/tmp/ptvcatalog-XXXXXX";
  int fd=mkstemp(file);assert(fd>=0);close(fd);
  FILE* out=fopen(file,"wb");assert(out);
  fwrite(json.data(),1,json.size(),out);fclose(out);
  auto loaded=load_file(file);
  assert(loaded.loaded && loaded.tv.size()==3);
  remove(file);
  auto missing=load_file(file);
  assert(!missing.loaded&&!missing.error.empty());
  puts("TMDB catalog parser tests passed");
}
