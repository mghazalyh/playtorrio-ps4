#pragma once
#include <string>
#include "../src/core.hpp"
namespace ps4source {
struct Selection {std::string url, reason; int sources=0; bool from_torbox=false;};
Selection resolve(const ptv::Settings& s,
                  const std::string& mediaType="",
                  const std::string& mediaId="");
// Values are redacted in log callers; never log url, tokens, magnet or names.
}
