#pragma once
#include "../src/catalog.hpp"
#include "../src/show_details.hpp"
#include "../src/core.hpp"
namespace ptvcatalog {
Catalog fetch_online(const ptv::Settings& settings);
// Real TMDB recommendation/similar items; no invented relevance percentages.
std::vector<Item> fetch_related(const ptv::Settings&,bool series,int tmdbId);
// Fetch IMDb ID only when the user selects a title outside the initial page.
std::string imdb_for_tmdb(const ptv::Settings& settings,bool series,int tmdbId);
}

namespace ptvcatalog {
// Network is performed by caller-owned background threads.
bool fetch_seasons(const ptv::Settings& cfg,int tmdbId,
                   std::vector<ptvdetail::Season>& result);
bool fetch_episodes(const ptv::Settings& cfg,int tmdbId,int season,
                    std::vector<ptvdetail::Episode>& result);
}
