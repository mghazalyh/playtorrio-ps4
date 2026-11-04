#pragma once
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <string>
#include <vector>
#include "../vendor/picojson.h"

#ifndef PTV_PS4_BUILD_COMMIT
#define PTV_PS4_BUILD_COMMIT "unknown"
#endif
#ifndef PTV_PS4_BUILD_RUN
#define PTV_PS4_BUILD_RUN "0"
#endif

namespace ptvtelemetry {
enum class Code {
  SettingsLoaded, SourceRequested, SourceResolutionFailed, PlaybackStarted,
  HeadFailed, RangeProbeFailed, RemoteOpenFailed, RemoteReadFailed,
  RemoteRangeFetched, UnsupportedScheme, UnsupportedHls,
  AvModuleFailed, AvInitFailed, AvSourceFailed, Buffering, DecoderTimeout,
  InvalidFrame, InvalidCrop, ConversionFailed, FirstFrame, PlaybackProgress,
  AudioModuleFailed, AudioInitFailed, AudioPortFailed, AudioUnsupportedFormat,
  AudioOutputFailed, FirstAudioBuffer,
  PlaybackStopped, PlaybackEnded, PlaybackPaused, PlaybackResumed,
  PlaybackStall
};
inline const char* name(Code code) {
  switch(code) {
    case Code::SettingsLoaded: return "settings_loaded";
    case Code::SourceRequested: return "source_requested";
    case Code::SourceResolutionFailed: return "source_resolution_failed";
    case Code::PlaybackStarted: return "playback_started";
    case Code::HeadFailed: return "head_failed";
    case Code::RangeProbeFailed: return "range_probe_failed";
    case Code::RemoteOpenFailed: return "remote_open_failed";
    case Code::RemoteReadFailed: return "remote_read_failed";
    case Code::RemoteRangeFetched: return "remote_range_fetched";
    case Code::UnsupportedScheme: return "unsupported_scheme";
    case Code::UnsupportedHls: return "unsupported_hls";
    case Code::AvModuleFailed: return "avplayer_module_failed";
    case Code::AvInitFailed: return "avplayer_init_failed";
    case Code::AvSourceFailed: return "avplayer_source_failed";
    case Code::Buffering: return "playback_buffering";
    case Code::DecoderTimeout: return "decoder_timeout";
    case Code::InvalidFrame: return "invalid_frame";
    case Code::InvalidCrop: return "invalid_crop";
    case Code::ConversionFailed: return "nv12_conversion_failed";
    case Code::FirstFrame: return "first_decoded_frame";
    case Code::PlaybackProgress: return "playback_progress";
    case Code::AudioModuleFailed: return "audio_module_failed";
    case Code::AudioInitFailed: return "audio_init_failed";
    case Code::AudioPortFailed: return "audio_port_failed";
    case Code::AudioUnsupportedFormat: return "audio_format_unsupported";
    case Code::AudioOutputFailed: return "audio_output_failed";
    case Code::FirstAudioBuffer: return "first_audio_buffer";
    case Code::PlaybackStopped: return "playback_stopped";
    case Code::PlaybackEnded: return "playback_ended";
    case Code::PlaybackPaused: return "playback_paused";
    case Code::PlaybackResumed: return "playback_resumed";
    case Code::PlaybackStall: return "playback_stall";
  }
  return "unknown";
}
inline const char* stage(Code code) {
  switch(code) {
    case Code::SettingsLoaded: return "settings";
    case Code::SourceRequested:
    case Code::SourceResolutionFailed: return "source";
    case Code::HeadFailed:
    case Code::RangeProbeFailed:
    case Code::RemoteOpenFailed:
    case Code::RemoteReadFailed:
    case Code::RemoteRangeFetched: return "video_io";
    case Code::AvModuleFailed:
    case Code::AvInitFailed:
    case Code::AvSourceFailed:
    case Code::DecoderTimeout:
    case Code::InvalidFrame:
    case Code::InvalidCrop:
    case Code::ConversionFailed: return "decoder";
    case Code::AudioModuleFailed:
    case Code::AudioInitFailed:
    case Code::AudioPortFailed:
    case Code::AudioUnsupportedFormat:
    case Code::AudioOutputFailed:
    case Code::FirstAudioBuffer: return "audio";
    default: return "playback";
  }
}
inline bool failure(Code code) {
  switch(code) {
    case Code::SettingsLoaded:
    case Code::SourceRequested:
    case Code::PlaybackStarted:
    case Code::RemoteRangeFetched:
    case Code::Buffering:
    case Code::FirstFrame:
    case Code::PlaybackProgress:
    case Code::FirstAudioBuffer:
    case Code::PlaybackStopped:
    case Code::PlaybackEnded:
    case Code::PlaybackPaused:
    case Code::PlaybackResumed: return false;
    case Code::PlaybackStall: return true;
    default: return true;
  }
}
struct Event {
  Code code=Code::SettingsLoaded;
  std::time_t observed_at=0;
  int http_status=0;
  int native_error=0;
  uint64_t count=0;
};
// Numeric-only session snapshot. Never includes titles, URLs, content,
// torrent hashes, console IDs, tokens, text or stable device fingerprints.
struct PlaybackMetrics {
  uint64_t decoder_width=0,decoder_height=0,pitch=0;
  uint64_t visible_width=0,visible_height=0;
  uint64_t output_width=0,output_height=0;
  bool full_range=false;
  uint64_t frames=0,elapsed_ms=0,effective_fps_x100=0;
  uint64_t avg_convert_us=0,max_convert_us=0,avg_blit_us=0;
  uint64_t max_frame_gap_ms=0,stall_count=0;
  uint64_t range_requests=0,decoder_read_calls=0,audio_buffers=0;
  uint64_t audio_channels=0,audio_sample_rate_hz=0;
  bool audio_format_unsupported=false;
};
// Five-second, bounded, privacy-safe performance history.
struct PerformanceSample {
  std::time_t observed_at=0;
  PlaybackMetrics metrics{};
  uint64_t frame_delta=0;
};
inline double limited(uint64_t number){
  return double(std::min<uint64_t>(number,1000000000ULL));
}
// No free text can reach the serialized report; even failure codes are typed.
inline std::string report(const std::vector<Event>& events,
                          const PlaybackMetrics& m=PlaybackMetrics{},
                          const std::vector<PerformanceSample>& performance={}) {
  picojson::object root;
  const auto timestamp=std::time(nullptr);
  std::tm utc={};
  char utcText[32]={};
#if defined(_WIN32)
  const bool hasUtc=gmtime_s(&utc,&timestamp)==0;
#else
  const bool hasUtc=gmtime_r(&timestamp,&utc)!=nullptr;
#endif
  if(hasUtc)std::strftime(utcText,sizeof(utcText),"%Y-%m-%dT%H:%M:%SZ",&utc);
  root["timestampUtc"]=picojson::value(hasUtc?utcText:"unavailable");
  root["appVersion"]=picojson::value("1.5.0-experimental");
  root["buildCommit"]=picojson::value(PTV_PS4_BUILD_COMMIT);
  root["buildRun"]=picojson::value(PTV_PS4_BUILD_RUN);
  root["schema"]=picojson::value(1.0);
  root["kind"]=picojson::value("ps4_playback_capability_diagnostic");
  root["platform"]=picojson::value("ps4");
  root["reason"]=picojson::value("automatic_playback_diagnostic");
  picojson::object metrics;
  metrics["decoder_width"]=picojson::value(limited(m.decoder_width));
  metrics["decoder_height"]=picojson::value(limited(m.decoder_height));
  metrics["pitch"]=picojson::value(limited(m.pitch));
  metrics["visible_width"]=picojson::value(limited(m.visible_width));
  metrics["visible_height"]=picojson::value(limited(m.visible_height));
  metrics["output_width"]=picojson::value(limited(m.output_width));
  metrics["output_height"]=picojson::value(limited(m.output_height));
  metrics["full_range"]=picojson::value(m.full_range);
  metrics["frames"]=picojson::value(limited(m.frames));
  metrics["elapsed_ms"]=picojson::value(limited(m.elapsed_ms));
  metrics["effective_fps_x100"]=picojson::value(limited(m.effective_fps_x100));
  metrics["avg_convert_us"]=picojson::value(limited(m.avg_convert_us));
  metrics["max_convert_us"]=picojson::value(limited(m.max_convert_us));
  metrics["avg_blit_us"]=picojson::value(limited(m.avg_blit_us));
  metrics["max_frame_gap_ms"]=picojson::value(limited(m.max_frame_gap_ms));
  metrics["stall_count"]=picojson::value(limited(m.stall_count));
  metrics["range_requests"]=picojson::value(limited(m.range_requests));
  metrics["decoder_read_calls"]=picojson::value(limited(m.decoder_read_calls));
  metrics["audio_buffers"]=picojson::value(limited(m.audio_buffers));
  metrics["audio_channels"]=picojson::value(limited(m.audio_channels));
  metrics["audio_sample_rate_hz"]=picojson::value(limited(m.audio_sample_rate_hz));
  metrics["audio_format_unsupported"]=picojson::value(m.audio_format_unsupported);
  root["playback"]=picojson::value(metrics);
  root["sampleIntervalSeconds"]=picojson::value(5.0);
  picojson::array history;
  const size_t begin=performance.size()>24?performance.size()-24:0;
  for(size_t i=begin;i<performance.size();++i){
    const auto& p=performance[i];
    const auto& v=p.metrics;
    picojson::object sample;
    sample["unix_seconds"]=picojson::value(double(std::max<std::time_t>(0,p.observed_at)));
    sample["frames"]=picojson::value(limited(v.frames));
    sample["frame_delta"]=picojson::value(limited(p.frame_delta));
    sample["elapsed_ms"]=picojson::value(limited(v.elapsed_ms));
    sample["effective_fps_x100"]=picojson::value(limited(v.effective_fps_x100));
    sample["decoder_width"]=picojson::value(limited(v.decoder_width));
    sample["decoder_height"]=picojson::value(limited(v.decoder_height));
    sample["output_width"]=picojson::value(limited(v.output_width));
    sample["output_height"]=picojson::value(limited(v.output_height));
    sample["avg_convert_us"]=picojson::value(limited(v.avg_convert_us));
    sample["max_convert_us"]=picojson::value(limited(v.max_convert_us));
    sample["avg_blit_us"]=picojson::value(limited(v.avg_blit_us));
    sample["max_frame_gap_ms"]=picojson::value(limited(v.max_frame_gap_ms));
    sample["stall_count"]=picojson::value(limited(v.stall_count));
    sample["range_requests"]=picojson::value(limited(v.range_requests));
    sample["decoder_read_calls"]=picojson::value(limited(v.decoder_read_calls));
    sample["audio_buffers"]=picojson::value(limited(v.audio_buffers));
    sample["audio_channels"]=picojson::value(limited(v.audio_channels));
    sample["audio_sample_rate_hz"]=picojson::value(limited(v.audio_sample_rate_hz));
    sample["audio_format_unsupported"]=picojson::value(v.audio_format_unsupported);
    history.emplace_back(sample);
  }
  root["performanceSamples"]=picojson::value(history);
  picojson::array samples;
  const size_t first=events.size()>24?events.size()-24:0;
  for(size_t i=first;i<events.size();++i) {
    const auto& e=events[i];
    picojson::object sample;
    sample["stage"]=picojson::value(stage(e.code));
    sample["code"]=picojson::value(name(e.code));
    sample["failure"]=picojson::value(failure(e.code));
    sample["unix_seconds"]=picojson::value(double(std::max<std::time_t>(0,e.observed_at)));
    sample["http_status"]=picojson::value(double(std::max(0,std::min(599,e.http_status))));
    sample["native_error"]=picojson::value(double(e.native_error));
    sample["count"]=picojson::value(double(std::min<uint64_t>(1000000000ULL,e.count)));
    samples.emplace_back(sample);
  }
  root["events"]=picojson::value(samples);
  return picojson::value(root).serialize();
}
inline bool safe_token(const std::string& token) {
  if(token.empty()||token.size()>1024)return false;
  for(unsigned char c:token)if(c<33||c>126)return false;
  return true;
}
}
