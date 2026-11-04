#include "diagnostics.hpp"
#include "native_net.hpp"
#include "../src/telemetry_policy.hpp"
#include "../vendor/picojson.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ps4diag {
namespace {
struct Item { uint64_t serial; ptvtelemetry::Event event; };
class AutomaticUploader {
public:
  ~AutomaticUploader() {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      stopping_=true;
    }
    cv_.notify_all();
    if(worker_.joinable())worker_.join();
  }
  void configure(const ptv::Settings& settings) {
    std::string token=settings.telemetry_token.empty()
      ?settings.github_token:settings.telemetry_token;
    // Intentionally independent of githubSyncEnabled. No library activity.
    const bool enabled=settings.telemetry_enabled&&ptvtelemetry::safe_token(token);
    bool active=false;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      enabled_=enabled;
      token_=enabled?std::move(token):std::string();
      if(enabled_&&!sessionNonce_)sessionNonce_=static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
      if(!enabled_)sessionNonce_=0;
      if(!enabled_){events_.clear();samples_.clear();metrics_={};pending_=false;
        last_sample_at_={};last_sample_metrics_={};uploadHealth_={};}
      if(enabled_&&!worker_.joinable()) {
        try {worker_=std::thread([this]{loop();});}
        catch(const std::exception&){enabled_=false;token_.clear();}
      }
      active=enabled_;
    }
    cv_.notify_all();
    note("TELEMETRY",active?"INFO private auto diagnostics opted in":
         "INFO private auto diagnostics disabled");
  }
  void record(ptvtelemetry::Code code,int http,int native,uint64_t count) {
    std::lock_guard<std::mutex> guard(mutex_);
    if(!enabled_||stopping_)return;
    if(code==ptvtelemetry::Code::SourceRequested ||
       code==ptvtelemetry::Code::PlaybackStarted){
      samples_.clear();last_sample_at_={};last_sample_metrics_={};metrics_={};
    }
    if(events_.size()>=24)events_.erase(events_.begin());
    events_.push_back({++serial_,{code,std::time(nullptr),http,native,count}});
    // An opted-in session leaves a report even if nothing fails.
    // Progress events already occur every 120 decoded frames; the worker
    // limits upload attempts to one per 30 seconds.
    if(ptvtelemetry::failure(code) ||
       code==ptvtelemetry::Code::SettingsLoaded ||
       code==ptvtelemetry::Code::PlaybackStarted ||
       code==ptvtelemetry::Code::FirstFrame ||
       code==ptvtelemetry::Code::PlaybackProgress ||
       code==ptvtelemetry::Code::PlaybackStopped ||
       code==ptvtelemetry::Code::PlaybackEnded) {
      pending_=true;
      cv_.notify_one();
    }
  }
  void update(const ptvtelemetry::PlaybackMetrics& metrics) {
    std::lock_guard<std::mutex> guard(mutex_);
    if(!enabled_||stopping_)return;
    metrics_=metrics;
    const auto now=std::chrono::steady_clock::now();
    if(last_sample_at_==std::chrono::steady_clock::time_point{}||
       now-last_sample_at_>=std::chrono::seconds(5)){
      ptvtelemetry::PerformanceSample sample;
      sample.observed_at=std::time(nullptr);
      sample.metrics=metrics;
      sample.frame_delta=ptvtelemetry::metric_delta(metrics.frames,last_sample_metrics_.frames);
      sample.presented_delta=ptvtelemetry::metric_delta(
        metrics.video_presented_frames,last_sample_metrics_.video_presented_frames);
      sample.skipped_delta=ptvtelemetry::metric_delta(
        metrics.video_skipped_frames,last_sample_metrics_.video_skipped_frames);
      sample.audio_buffers_delta=ptvtelemetry::metric_delta(
        metrics.audio_buffers,last_sample_metrics_.audio_buffers);
      sample.range_bytes_delta=ptvtelemetry::metric_delta(
        metrics.range_downloaded_bytes,last_sample_metrics_.range_downloaded_bytes);
      sample.range_retries_delta=ptvtelemetry::metric_delta(
        metrics.range_retry_attempts,last_sample_metrics_.range_retry_attempts);
      sample.audio_failures_delta=ptvtelemetry::metric_delta(
        metrics.audio_output_failures,last_sample_metrics_.audio_output_failures);
      sample.conversion_failures_delta=ptvtelemetry::metric_delta(
        metrics.video_conversion_failures,last_sample_metrics_.video_conversion_failures);
      if(last_sample_at_!=std::chrono::steady_clock::time_point{}){
        sample.interval_ms=static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now-last_sample_at_).count());
        sample.decoded_fps_interval_x100=ptvtelemetry::interval_fps_x100(
          sample.frame_delta,sample.interval_ms);
        sample.presented_fps_interval_x100=ptvtelemetry::interval_fps_x100(
          sample.presented_delta,sample.interval_ms);
      }
      if(samples_.size()>=24)samples_.erase(samples_.begin());
      samples_.push_back(sample);
      last_sample_at_=now;
      last_sample_metrics_=metrics;
      // Five-second samples continue through buffering or quiet playback.
      // The uploader enforces 30-second HTTP pacing regardless of UI FPS.
      pending_=true;cv_.notify_one();
    }
  }
private:
  bool send(const std::vector<ptvtelemetry::Event>& events,
            const ptvtelemetry::PlaybackMetrics& metrics,
            const std::vector<ptvtelemetry::PerformanceSample>& samples,
            const std::string& token,uint64_t serial,uint64_t session,
            const ptvtelemetry::UploadHealth& upload,
            int& http_status,int& native_error) {
    if(!ptvtelemetry::upload_payload_available(events.size(),samples.size())||
       !ptvtelemetry::safe_token(token))return false;
    const std::time_t now=std::time(nullptr);
    std::tm utc={};
    char date[32]={};
    if(gmtime_r(&now,&utc))
      std::strftime(date,sizeof(date),"%Y-%m-%dT%H-%M-%SZ",&utc);
    else std::snprintf(date,sizeof(date),"utc-unknown");
    // Ephemeral monotonic nonce, never a console identifier or media ID.
    const auto nonce=static_cast<unsigned long long>(
      std::chrono::steady_clock::now().time_since_epoch().count());
    char filename[144]={};
    std::snprintf(filename,sizeof(filename),"%s-%llx-%llu.json",date,
      nonce,static_cast<unsigned long long>(serial));
    char folder[16]={};
    if(gmtime_r(&now,&utc))
      std::strftime(folder,sizeof(folder),"%Y/%m/%d",&utc);
    else std::snprintf(folder,sizeof(folder),"undated");
    const std::string path="playtorrio/diagnostics/ps4/player/"+
      std::string(folder)+"/"+std::string(filename);
    const std::string url=
      "https://api.github.com/repos/mghazalyh/torriodb/contents/"+path;
    picojson::object body;
    body["message"]=picojson::value("diagnostics(ps4): allowlisted player report");
    body["branch"]=picojson::value("main");
    body["content"]=picojson::value(ptv::base64_encode(ptvtelemetry::report(events,metrics,samples,session,upload)));
    try {
      const auto response=ps4net::request(url,"PUT",{
        {"Authorization","Bearer "+token},
        {"Accept","application/vnd.github+json"},
        {"Content-Type","application/json"},
        {"X-GitHub-Api-Version","2022-11-28"},
        {"User-Agent","PlayTorrio-PS4-private-diagnostics"}
      },picojson::value(body).serialize(),32768);
      http_status=response.status;
      native_error=response.platform_error;
      const bool receipt= !response.truncated&&
        ptvtelemetry::github_create_receipt(response.body,path);
      const bool success=response.status==201&&response.platform_error==0&&receipt;
      note("TELEMETRY",std::string(success?"PASS":"FAIL")+
        " private GitHub upload http="+std::to_string(response.status)+
        " native="+std::to_string(response.platform_error)+
        " receipt="+std::to_string(receipt?1:0)+
        " events="+std::to_string(events.size())+
        " samples="+std::to_string(samples.size()));
      return success;
    }catch(const std::exception&){
      note("TELEMETRY","FAIL private GitHub upload exception");
    }catch(...){
      note("TELEMETRY","FAIL private GitHub upload unknown exception");
    }
    return false;
  }
  void loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for(;;) {
      cv_.wait(lock,[this]{return stopping_||(enabled_&&pending_);});
      if(stopping_)return;
      const auto next=last_attempt_+std::chrono::seconds(30);
      if(last_attempt_!=std::chrono::steady_clock::time_point{} &&
         std::chrono::steady_clock::now()<next) {
        cv_.wait_until(lock,next,[this]{return stopping_||!enabled_;});
        continue;
      }
      std::vector<ptvtelemetry::Event> batch;
      batch.reserve(events_.size());
      for(const auto& item:events_)batch.push_back(item.event);
      const uint64_t through=events_.empty()?0:events_.back().serial;
      const std::string token=token_;
      const auto metrics=metrics_;
      const auto samples=samples_;
      const auto session=sessionNonce_;
      ++uploadHealth_.attempts;
      const auto health=uploadHealth_;
      pending_=false;
      last_attempt_=std::chrono::steady_clock::now();
      lock.unlock();
      int status=0,native=0;
      const bool sent=send(batch,metrics,samples,token,through,session,
                           health,status,native);
      lock.lock();
      uploadHealth_.last_http_status=status;
      uploadHealth_.last_native_error=native;
      if(sent) {
        ++uploadHealth_.successes;
        events_.erase(std::remove_if(events_.begin(),events_.end(),
          [through](const Item& item){return item.serial<=through;}),
          events_.end());
      }else if(enabled_) {
        ++uploadHealth_.failures;
        // A failed upload must not vanish if this session has no subsequent
        // failure. Retry at the normal 30-second ceiling while the app runs.
        // This remains bounded in memory; no unbounded queue or retry storm.
        pending_=true;
      }
    }
  }
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  std::vector<Item> events_;
  ptvtelemetry::PlaybackMetrics metrics_{};
  std::vector<ptvtelemetry::PerformanceSample> samples_;
  std::chrono::steady_clock::time_point last_sample_at_{};
  ptvtelemetry::PlaybackMetrics last_sample_metrics_{};
  ptvtelemetry::UploadHealth uploadHealth_{};
  std::string token_;
  uint64_t serial_=0,sessionNonce_=0;
  bool stopping_=false,enabled_=false,pending_=false;
  std::chrono::steady_clock::time_point last_attempt_{};
};
AutomaticUploader& uploader() {static AutomaticUploader instance;return instance;}
}
void configure_telemetry(const ptv::Settings& settings){uploader().configure(settings);}
void playback_metrics(const ptvtelemetry::PlaybackMetrics& metrics) {
  uploader().update(metrics);
}
void event(ptvtelemetry::Code code,int http_status,int native_error,uint64_t count){
  uploader().record(code,http_status,native_error,count);
}
}
