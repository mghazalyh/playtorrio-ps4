#pragma once
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include "../vendor/picojson.h"

namespace ptvtelemetry {
enum class Code {
  SettingsLoaded, SourceRequested, SourceResolutionFailed, PlaybackStarted,
  HeadFailed, RangeProbeFailed, RemoteOpenFailed, RemoteReadFailed,
  RemoteRangeFetched, UnsupportedScheme, UnsupportedHls,
  AvModuleFailed, AvInitFailed, AvSourceFailed, Buffering, DecoderTimeout,
  InvalidFrame, InvalidCrop, ConversionFailed, FirstFrame, PlaybackProgress,
  AudioModuleFailed, AudioInitFailed, AudioPortFailed, AudioUnsupportedFormat,
  AudioOutputFailed, FirstAudioBuffer
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
    case Code::FirstAudioBuffer: return false;
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
// No free text can reach the serialized report; even failure codes are typed.
inline std::string report(const std::vector<Event>& events) {
  picojson::object root;
  root["schema"]=picojson::value(1.0);
  root["kind"]=picojson::value("ps4_playback_capability_diagnostic");
  root["platform"]=picojson::value("ps4");
  root["reason"]=picojson::value("playback_failure");
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
