#pragma once
#include <map>
#include <cstdint>
#include <string>

namespace ps4net {
struct Response {
  int status=0;
  int platform_error=0;
  int last_errno=0;
  std::string content_range;
  std::string accept_ranges;
  uint64_t content_length=0;
  bool has_content_length=false;
  bool truncated=false;
  std::string body;
};
bool init(int& error);
Response request(const std::string& url,const std::string& method="GET",
    const std::map<std::string,std::string>& headers={},
    const std::string& body="",size_t limit=2*1024*1024);
void shutdown();
}
