#include "../src/core.hpp"
#include "../src/telemetry_policy.hpp"
#include "../vendor/picojson.h"
#include <cassert>
#include <string>
#include <vector>

int main() {
  ptv::Settings defaults;
  std::string error;
  assert(ptv::import_settings("{}",defaults,error));
  assert(!defaults.telemetry_enabled);
  assert(defaults.telemetry_token.empty());
  assert(!defaults.github_sync_enabled);
  ptv::Settings configured;
  assert(ptv::import_settings(
    R"({"telemetryEnabled":true,"telemetryToken":"example_token","githubSyncEnabled":false})",
    configured,error));
  assert(configured.telemetry_enabled);
  assert(configured.telemetry_token=="example_token");
  assert(!configured.github_sync_enabled);
  assert(ptvtelemetry::safe_token(configured.telemetry_token));
  assert(!ptvtelemetry::safe_token(""));
  assert(!ptvtelemetry::safe_token("contains space"));
  assert(!ptvtelemetry::safe_token("line\nbreak"));
  ptv::Settings malformed;
  assert(ptv::import_settings(
    R"({"telemetryEnabled":"true","telemetryToken":"bad token"})",
    malformed,error));
  assert(!malformed.telemetry_enabled);
  assert(malformed.telemetry_token.empty());
  using ptvtelemetry::Code;
  assert(ptvtelemetry::failure(Code::RemoteReadFailed));
  assert(ptvtelemetry::failure(Code::DecoderTimeout));
  assert(!ptvtelemetry::failure(Code::FirstFrame));
  assert(std::string(ptvtelemetry::name(Code::RangeProbeFailed))=="range_probe_failed");
  const std::vector<ptvtelemetry::Event> events={
    {Code::SettingsLoaded,1730419200,0,0,0},
    {Code::RemoteReadFailed,1730419201,429,-12,125}
  };
  const auto json=ptvtelemetry::report(events);
  assert(json.find("example_token")==std::string::npos);
  assert(json.find("https://")==std::string::npos);
  assert(json.find("playtorrio/watchlist")==std::string::npos);
  picojson::value root;
  assert(picojson::parse(root,json).empty());
  const auto& object=root.get<picojson::object>();
  assert(object.at("platform").get<std::string>()=="ps4");
  assert(object.at("schema").get<double>()==1.0);
  const auto& records=object.at("events").get<picojson::array>();
  assert(records.size()==2);
  assert(records[1].get<picojson::object>().at("code").get<std::string>()=="remote_read_failed");
  assert(records[1].get<picojson::object>().at("http_status").get<double>()==429);
  assert(records[1].get<picojson::object>().at("failure").get<bool>());
  std::vector<ptvtelemetry::Event> many(26,events[1]);
  picojson::value again;
  assert(picojson::parse(again,ptvtelemetry::report(many)).empty());
  assert(again.get<picojson::object>().at("events").get<picojson::array>().size()==24);
  return 0;
}
