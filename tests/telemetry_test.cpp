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
  // Nonfatal sessions must still be eligible for periodic diagnostics.
  assert(!ptvtelemetry::failure(Code::PlaybackStopped));
  assert(!ptvtelemetry::failure(Code::PlaybackPaused));
  assert(ptvtelemetry::failure(Code::PlaybackStall));
  ptvtelemetry::PlaybackMetrics snapshot;
  snapshot.decoder_width=1920;
  snapshot.decoder_height=1080;
  snapshot.visible_width=1920;
  snapshot.visible_height=1080;
  snapshot.pitch=2048;
  snapshot.output_width=1920;
  snapshot.output_height=1080;
  snapshot.frames=240;
  snapshot.elapsed_ms=10000;
  snapshot.effective_fps_x100=2400;
  snapshot.avg_convert_us=12000;
  snapshot.max_convert_us=27000;
  snapshot.avg_blit_us=2500;
  snapshot.max_frame_gap_ms=1600;
  snapshot.stall_count=2;
  snapshot.range_requests=9;
  snapshot.audio_buffers=300;
  snapshot.audio_format_unsupported=true;
  picojson::value enriched;
  assert(picojson::parse(enriched,ptvtelemetry::report(events,snapshot)).empty());
  const auto& metrics=enriched.get<picojson::object>()
      .at("playback").get<picojson::object>();
  assert(metrics.at("decoder_width").get<double>()==1920);
  assert(metrics.at("output_height").get<double>()==1080);
  assert(metrics.at("effective_fps_x100").get<double>()==2400);
  assert(metrics.at("avg_convert_us").get<double>()==12000);
  assert(metrics.at("stall_count").get<double>()==2);
  assert(metrics.at("audio_format_unsupported").get<bool>());
  assert(ptvtelemetry::report(events,snapshot).find("https://")==std::string::npos);
  assert(ptvtelemetry::report(events,snapshot).find("example_token")==std::string::npos);
  snapshot.frames=UINT64_MAX;
  picojson::value clipped;
  assert(picojson::parse(clipped,ptvtelemetry::report(events,snapshot)).empty());
  assert(clipped.get<picojson::object>().at("playback").get<picojson::object>()
    .at("frames").get<double>()==1000000000.0);
  std::vector<ptvtelemetry::Event> many(26,events[1]);
  picojson::value again;
  assert(picojson::parse(again,ptvtelemetry::report(many)).empty());
  assert(again.get<picojson::object>().at("events").get<picojson::array>().size()==24);
  return 0;
}
