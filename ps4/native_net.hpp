#pragma once
#include <map>
#include <string>

namespace ps4net {
struct Response {
  int status=0;
  int platform_error=0;
  bool truncated=false;
  std::string body;
};
bool init(int& error);
Response request(const std::string& url,const std::string& method="GET",
    const std::map<std::string,std::string>& headers={},
    const std::string& body="",size_t limit=2*1024*1024);
void shutdown();
}
