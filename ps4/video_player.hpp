#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include "avplayer_abi.hpp"
#include "range_stream.hpp"
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
  // HD crop-aware bilinear NV12 conversion is still CPU-based; GPU texture
  // presentation is the planned longer-term backend (FPS is not guaranteed).
  bool render(Scene2D& canvas);
  Status state()const{return status_.load();}
  uint64_t frames()const{return frames_;}
  int lastError()const{return last_error_;}
  int bufferingSeconds()const {
    if(status_!=Status::Opening)return 0;
    return (int)std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now()-startTime_).count();
  }
private:
  static void* allocate(void*,uint32_t,uint32_t);
  static void deallocate(void*,void*);
  void audioLoop();
  static int openRemote(void*,const char*);
  static int closeRemote(void*);
  static int readRemote(void*,uint8_t*,uint64_t,uint32_t);
  static uint64_t remoteSize(void*);
  bool prepareRemote(const std::string& url,std::string& error);
  void publishMetrics();
  AvHandle handle_=nullptr;
  std::atomic<Status> status_{Status::Idle};
  std::chrono::steady_clock::time_point startTime_{};
  bool preferredTracks_=false;
  uint64_t frames_=0;
  uint64_t totalConvertUs_=0,totalBlitUs_=0,maxConvertUs_=0;
  uint64_t maxFrameGapMs_=0,stallCount_=0;
  std::chrono::steady_clock::time_point lastDecodedAt_{},lastStallEvent_{};
  int inputW_=0,inputH_=0,inputPitch_=0,visibleW_=0,visibleH_=0;
  bool fullRange_=false;
  std::atomic<uint64_t> audioBuffers_{0};
  std::atomic<uint64_t> audioChannels_{0},audioSampleRateHz_{0};
  std::atomic<bool> audioFormatUnsupported_{false};
  int last_error_=0;
  std::atomic<bool> audioRunning_{false};
  std::thread audioThread_;
  int user_=-1;
  RangeStream remote_;
  // Persist the last decoded picture while waiting for the next frame.
  std::vector<uint32_t> rgbPixels_;
  int rgbW_=0,rgbH_=0,rgbX_=0,rgbY_=0;
  std::atomic<uint64_t> remoteReads_{0};
  int lastProgressBucket_=-1;
  bool loggedInvalidFrame_=false;
};
}
