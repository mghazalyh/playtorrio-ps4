#pragma once
#include <string>
#include <vector>
#include "../src/core.hpp"
namespace ps4diag {
static const char* const logfile="/data/playtorrio/logs/diagnostics.log";
static const char* const previous="/data/playtorrio/logs/previous.log";
struct Summary {
  int passed=0,failed=0,skipped=0,addon_streams=0,library_titles=0;
  std::vector<std::string> preview_titles; // Shown locally on PS4, NEVER written to diagnostic log.
  std::string last="Not run yet";
};
bool start_logging();
void note(const std::string& component,const std::string& msg);
Summary run(const ptv::Settings& config, bool githubWriteCheck=true);
}
