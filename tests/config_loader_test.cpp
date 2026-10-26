#include "../src/config_loader.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
using ptv::SettingsFileStatus;
static void put(const char* file,const std::string& content) {
  FILE* f=std::fopen(file,"wb");
  assert(f);
  assert(std::fwrite(content.data(),1,content.size(),f)==content.size());
  assert(std::fclose(f)==0);
}
int main() {
  char path[]="/tmp/ptv-settings-test-XXXXXX";
  int fd=mkstemp(path);assert(fd>=0);close(fd);
  put(path,R"({"addons":["https://example.com/a/manifest.json"],"tmdbApiKey":"test-v3","githubSyncEnabled":false})");
  auto v=ptv::load_settings_file(path);
  assert(v.ok() && v.settings.addons.size()==1 && v.settings.tmdb_api_key=="test-v3");
  assert(!v.settings.github_sync_enabled);
  put(path,std::string("\xEF\xBB\xBF",3)+R"({"addons":["https://example.com/manifest.json"],"githubSyncEnabled":true})");
  v=ptv::load_settings_file(path);
  assert(v.ok() && v.settings.github_sync_enabled);
  put(path,R"({"addons":[)");
  v=ptv::load_settings_file(path);
  assert(v.status==SettingsFileStatus::InvalidJson);
  put(path,std::string("\xFF\xFE",2)+std::string("{\0}\0",4));
  v=ptv::load_settings_file(path);
  assert(v.status==SettingsFileStatus::InvalidEncoding);
  put(path,std::string(262145,'x'));
  v=ptv::load_settings_file(path);
  assert(v.status==SettingsFileStatus::TooLarge);
  put(path,"");
  v=ptv::load_settings_file(path);
  assert(v.status==SettingsFileStatus::Empty);
  std::remove(path);
  v=ptv::load_settings_file(path);
  assert(v.status==SettingsFileStatus::Missing);
  std::puts("Settings-loader tests passed: UTF8 BOM, malformed/oversize files, missing/empty and sync-disabled");
}
