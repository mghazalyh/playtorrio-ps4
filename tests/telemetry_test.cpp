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
  assert(ptvtelemetry::failure(Code::VideoWorkerFailed));
  assert(!ptvtelemetry::failure(Code::VideoWorkerStarted));
  assert(!ptvtelemetry::failure(Code::AudioPortReady));
  assert(!ptvtelemetry::failure(Code::AudioConverted));
  assert(std::string(ptvtelemetry::name(Code::VideoWorkerStarted))=="video_worker_started");
  assert(std::string(ptvtelemetry::name(Code::AudioConverted))=="audio_pcm_converted");
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
  assert(metrics.at("video_worker_enabled").get<bool>()==false);
  assert(metrics.at("video_empty_polls").get<double>()==0);
  assert(metrics.at("presented_fps_x100").get<double>()==0);
  assert(metrics.at("range_downloaded_bytes").get<double>()==0);
  assert(metrics.at("audio_channels").get<double>()==0);
  assert(metrics.at("audio_sample_rate_hz").get<double>()==0);
  assert(enriched.get<picojson::object>().at("timestampUtc").is<std::string>());
  assert(enriched.get<picojson::object>().at("buildCommit").is<std::string>());
  assert(enriched.get<picojson::object>().at("buildRun").is<std::string>());
  assert(enriched.get<picojson::object>().at("sampleIntervalSeconds").get<double>()==5.0);
  snapshot.audio_channels=6;snapshot.audio_sample_rate_hz=48000;
  std::vector<ptvtelemetry::PerformanceSample> history={
    {1730419200,snapshot,60},{1730419205,snapshot,120}
  };
  picojson::value sampled;
  assert(picojson::parse(sampled,ptvtelemetry::report(events,snapshot,history)).empty());
  const auto& steps=sampled.get<picojson::object>()
      .at("performanceSamples").get<picojson::array>();
  assert(steps.size()==2);
  assert(steps[1].get<picojson::object>().at("frame_delta").get<double>()==120);
  assert(steps[0].get<picojson::object>().at("interval_ms").get<double>()==0);
  assert(steps[0].get<picojson::object>().at("audio_channels").get<double>()==6);
  assert(steps[1].get<picojson::object>().at("audio_sample_rate_hz").get<double>()==48000);
  std::vector<ptvtelemetry::PerformanceSample> overflowing(31,history[0]);
  picojson::value cappedSamples;
  assert(picojson::parse(cappedSamples,ptvtelemetry::report(events,snapshot,overflowing)).empty());
  assert(cappedSamples.get<picojson::object>().at("performanceSamples")
      .get<picojson::array>().size()==24);
  assert(ptvtelemetry::report(events,snapshot,history).find("https://")==std::string::npos);
  snapshot.video_worker_enabled=true;
  snapshot.video_empty_polls=250;
  snapshot.video_presented_frames=96;
  snapshot.video_cached_redraws=12;
  snapshot.video_skipped_frames=4;
  snapshot.presented_fps_x100=2400;
  snapshot.audio_converted_chunks=13;
  snapshot.audio_output_failures=1;
  snapshot.range_downloaded_bytes=4194304;
  snapshot.range_retry_attempts=2;
  snapshot.range_rejected_blocks=1;
  snapshot.range_read_timeouts=0;
  picojson::value diagnostic;
  assert(picojson::parse(diagnostic,
    ptvtelemetry::report(events,snapshot,history,0x1234ULL)).empty());
  const auto& d=diagnostic.get<picojson::object>();
  assert(d.at("ephemeralSession").get<std::string>()=="0000000000001234");
  assert(d.at("videoBackend").get<std::string>()==
         "avplayer_cpu_sse2_worker_videoout");
  const auto& snapshotObject=d.at("playback").get<picojson::object>();
  assert(snapshotObject.at("video_worker_enabled").get<bool>());
  assert(snapshotObject.at("video_empty_polls").get<double>()==250);
  assert(snapshotObject.at("range_retry_attempts").get<double>()==2);
  assert(snapshotObject.at("range_downloaded_bytes").get<double>()==4194304);
  assert(snapshotObject.at("presented_fps_x100").get<double>()==2400);
  assert(snapshotObject.at("audio_converted_chunks").get<double>()==13);
  assert(snapshotObject.at("video_skipped_frames").get<double>()==4);
  assert(ptvtelemetry::report(events,snapshot).find("https://")==std::string::npos);
  assert(ptvtelemetry::report(events,snapshot).find("example_token")==std::string::npos);
  snapshot.frames=UINT64_MAX;
  picojson::value clipped;
  assert(picojson::parse(clipped,ptvtelemetry::report(events,snapshot)).empty());
  assert(clipped.get<picojson::object>().at("playback").get<picojson::object>()
    .at("frames").get<double>()==1000000000.0);
  assert(ptvtelemetry::upload_payload_available(0,1));
  assert(ptvtelemetry::upload_payload_available(1,0));
  assert(!ptvtelemetry::upload_payload_available(0,0));
  assert(ptvtelemetry::metric_delta(101,75)==26);
  assert(ptvtelemetry::metric_delta(9,99)==9);
  assert(ptvtelemetry::interval_fps_x100(120,5000)==2400);
  assert(ptvtelemetry::interval_fps_x100(50,0)==0);
  assert(ptvtelemetry::interval_fps_x100(UINT64_MAX,1)==100000000000000ULL);
  const std::string target="playtorrio/diagnostics/ps4/player/2026/11/05/test.json";
  const std::string receipt=std::string(R"({"content":{"path":")")+target+
    R"(","sha":"abc123"},"commit":{"sha":"123456"}})";
  assert(ptvtelemetry::github_create_receipt(receipt,target));
  assert(!ptvtelemetry::github_create_receipt(receipt,target+".other"));
  assert(!ptvtelemetry::github_create_receipt("{}",target));
  assert(!ptvtelemetry::github_create_receipt("not json",target));
  ptvtelemetry::UploadHealth retryHistory;
  retryHistory.attempts=3;retryHistory.successes=1;retryHistory.failures=2;
  retryHistory.last_http_status=401;
  picojson::value checkedHealth;
  assert(picojson::parse(checkedHealth,
     ptvtelemetry::report({},snapshot,history,0x1234ULL,retryHistory)).empty());
  const auto& uploadHealth=checkedHealth.get<picojson::object>()
     .at("uploadHealth").get<picojson::object>();
  assert(uploadHealth.at("attempts").get<double>()==3);
  assert(uploadHealth.at("failures").get<double>()==2);
  assert(uploadHealth.at("last_http_status").get<double>()==401);
  std::vector<ptvtelemetry::Event> many(26,events[1]);
  picojson::value again;
  assert(picojson::parse(again,ptvtelemetry::report(many)).empty());
  assert(again.get<picojson::object>().at("events").get<picojson::array>().size()==24);
  return 0;
}
