#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include "avplayer_abi.hpp"
class Scene2D;
namespace ps4video {
class Player {
public:
  enum class Status { Idle, Opening, Playing, Paused, Ended, Failed };
  Player()=default;
  ~Player(){close();}
  bool start(const std::string& url, std::string& error);
  void close();
  void pauseToggle();
  // Called on PS4 UI/render thread. Display most recent decoded NV12 frame.
  // The 2x2 block sampling is CPU-based; no quality or FPS guarantee.
  bool render(Scene2D& canvas);
  Status state()const{return status_.load();}
  uint64_t frames()const{return frames_;}
  int lastError()const{return last_error_;}
private:
  static void* allocate(void*,uint32_t,uint32_t);
  static void deallocate(void*,void*);
  void audioLoop();
  AvHandle handle_=nullptr;
  std::atomic<Status> status_{Status::Idle};
  std::chrono::steady_clock::time_point startTime_{};
  bool preferredTracks_=false;
  uint64_t frames_=0;
  int last_error_=0;
  std::atomic<bool> audioRunning_{false};
  std::thread audioThread_;
  int user_=-1;
};
}
