#pragma once
#include <string>
#include <vector>
namespace ptvcatalog {
struct Item {
  int tmdb_id=0;
  std::string title,overview,imdb_id,year;
  float rating=0;
  bool series=false;
};
struct Catalog {
  std::vector<Item> tv,movies;
  std::string updated;
  std::string error;
  bool loaded=false;
};
bool parse_catalog(const std::string& src,Catalog& result,std::string& error);
Catalog load_file(const char* path);
}
