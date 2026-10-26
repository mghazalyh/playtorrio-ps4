#pragma once
#include "../src/advanced_discover.hpp"
#include "../src/catalog.hpp"
#include "../src/core.hpp"
namespace ptvdiscover {
struct Page {
 std::vector<ptvcatalog::Item> titles;
 int number=1,total_pages=1,raw_total=0;
 bool loaded=false;
 std::string error;
};
Page fetch(const ptv::Settings& cfg,const Filters& filter);
}