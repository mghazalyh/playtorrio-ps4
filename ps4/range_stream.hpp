#pragma once
#include "../src/range_window.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ps4video {
class RangeStream {
public:
  ~RangeStream() { stop(); }
  RangeStream()=default;
  RangeStream(const RangeStream&)=delete;
  RangeStream& operator=(const RangeStream&)=delete;
  bool start(const std::string& url,uint64_t total,
             const std::string& head,std::string& error);
  void stop();
  int read(uint8_t* out,uint64_t pos,uint32_t length);
  uint64_t size()const {return total_.load();}
  uint64_t requests()const {return requests_.load();}
  uint64_t downloaded_bytes()const {return downloadedBytes_.load();}
  uint64_t retry_attempts()const {return retryAttempts_.load();}
  uint64_t read_timeouts()const {return readTimeouts_.load();}
  uint64_t rejected_ranges()const {return rejectedRanges_.load();}
  bool ready()const {return active_.load();}
private:
  void workerLoop();
  std::string url_;
  std::string prefix_; // 64 KiB validated initial HTTP 206 probe
  std::map<uint64_t,std::vector<uint8_t>> blocks_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::thread worker_;
  std::atomic<uint64_t> total_{0},requests_{0};
  std::atomic<uint64_t> downloadedBytes_{0},retryAttempts_{0};
  std::atomic<uint64_t> readTimeouts_{0},rejectedRanges_{0};
  std::atomic<bool> active_{false};
  uint64_t wanted_=0,suppressed_=ptvrange::kNone;
  bool stopping_=true,failed_=false;
};
}
