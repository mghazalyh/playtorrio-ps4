#pragma once
#include "../src/catalog.hpp"
#include "../src/core.hpp"
namespace ptvcatalog {
Catalog fetch_online(const ptv::Settings& settings);
// Fetch IMDb ID only when the user selects a title outside the initial page.
std::string imdb_for_tmdb(const ptv::Settings& settings,bool series,int tmdbId);
}
