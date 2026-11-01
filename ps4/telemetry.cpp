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
      if(!enabled_){events_.clear();metrics_={};pending_=false;}
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
    if(enabled_&&!stopping_)metrics_=metrics;
  }
private:
  bool send(const std::vector<ptvtelemetry::Event>& events,
            const ptvtelemetry::PlaybackMetrics& metrics,
            const std::string& token,uint64_t serial) {
    if(events.empty()||!ptvtelemetry::safe_token(token))return false;
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
    const std::string path="playtorrio/diagnostics/ps4/player/"+std::string(filename);
    const std::string url=
      "https://api.github.com/repos/mghazalyh/torriodb/contents/"+path;
    picojson::object body;
    body["message"]=picojson::value("diagnostics(ps4): allowlisted player report");
    body["branch"]=picojson::value("main");
    body["content"]=picojson::value(ptv::base64_encode(ptvtelemetry::report(events,metrics)));
    try {
      const auto response=ps4net::request(url,"PUT",{
        {"Authorization","Bearer "+token},
        {"Accept","application/vnd.github+json"},
        {"Content-Type","application/json"},
        {"X-GitHub-Api-Version","2022-11-28"},
        {"User-Agent","PlayTorrio-PS4-private-diagnostics"}
      },picojson::value(body).serialize(),32768);
      const bool success=response.status==201&&response.platform_error==0;
      note("TELEMETRY",std::string(success?"PASS":"FAIL")+
        " private GitHub upload http="+std::to_string(response.status)+
        " native="+std::to_string(response.platform_error)+
        " events="+std::to_string(events.size()));
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
      pending_=false;
      last_attempt_=std::chrono::steady_clock::now();
      lock.unlock();
      const bool sent=send(batch,metrics,token,through);
      lock.lock();
      if(sent) {
        events_.erase(std::remove_if(events_.begin(),events_.end(),
          [through](const Item& item){return item.serial<=through;}),
          events_.end());
      }else if(enabled_) {
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
  std::string token_;
  uint64_t serial_=0;
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
