#pragma once
// Bounded asynchronous TMDB artwork; one decoded size in a small memory cache.
// No network/codec work occurs on the PS4 render thread.
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
class Scene2D;
namespace ps4art {
struct Image {int w=0,h=0;std::vector<uint32_t> pixels;};
class Cache {
public:
  bool draw(Scene2D& scene,const std::string& tmdbPath,int x,int y,int w,int h);
  // Launches work for only currently visible artwork. Never fetches private URLs.
  void request(const std::string& tmdbPath);
private:
  struct Entry {std::shared_ptr<Image> image;bool loading=false,failed=false;uint64_t seen=0;};
  std::mutex lock_;
  std::map<std::string,Entry> entries_;
  int workers_=0;
  uint64_t tick_=0;
  std::vector<uint32_t> scratch_; // render-thread only
};
}
