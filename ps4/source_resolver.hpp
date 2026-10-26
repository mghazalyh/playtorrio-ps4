#pragma once
#include <string>
#include <vector>
#include "../src/core.hpp"
namespace ps4source {
struct Selection {std::string url, reason; int sources=0; bool from_torbox=false;};
struct Candidate {
  ptv::Stream stream;
  int score=0;
  bool direct=false;
};
struct Choices {
  std::vector<Candidate> entries;
  std::string error;
  int found=0;
};
// Source discovery is read-only: no TorBox torrent creation until selected.
Choices list_sources(const ptv::Settings&,const std::string& type,const std::string& id);
// The user explicitly selecting a source authorizes its TorBox resolution.
Selection resolve_selected(const ptv::Settings&,const Candidate&);
Selection resolve(const ptv::Settings& s,
                  const std::string& mediaType="",
                  const std::string& mediaId="");
// Values are redacted in log callers; never log url, tokens, magnet or names.
}
