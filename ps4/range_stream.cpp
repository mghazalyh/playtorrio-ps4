#include "range_stream.hpp"
#include "diagnostics.hpp"
#include "native_net.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <set>
#include <utility>

namespace ps4video {
bool RangeStream::start(const std::string& url,uint64_t total,
                        const std::string& head,std::string& error) {
  stop();
  if(url.rfind("https://",0)!=0||total==0||
     total>(uint64_t)INT64_MAX||head.empty()||head.size()>65536||
     head.size()>total){
    error="Invalid seekable stream metadata";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    url_=url;prefix_=head;total_=total;requests_=0;
    wanted_=0;suppressed_=ptvrange::kNone;
    stopping_=false;failed_=false;active_=true;
  }
  try {
    worker_=std::thread(&RangeStream::workerLoop,this);
  }catch(const std::exception&){
    error="Could not start independent streaming network worker";
    stop();return false;
  }catch(...){
    error="Unknown streaming worker initialization failure";
    stop();return false;
  }
  return true;
}
void RangeStream::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_=true;
    active_=false;
  }
  changed_.notify_all();
  if(worker_.joinable())worker_.join();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    blocks_.clear();
    url_.clear();prefix_.clear();
    total_=0;requests_=0;failed_=false;wanted_=0;
    suppressed_=ptvrange::kNone;
  }
}
int RangeStream::read(uint8_t* out,uint64_t pos,uint32_t length) {
  if(!out||!length)return -1;
  std::unique_lock<std::mutex> lock(mutex_);
  if(stopping_||failed_||!active_)return -1;
  if(pos>=total_)return 0;
  const uint32_t want=(uint32_t)std::min<uint64_t>(length,total_-pos);
  uint32_t copied=0;
  while(copied<want) {
    const uint64_t offset=pos+copied;
    if(offset<prefix_.size()) {
      const size_t avail=std::min<size_t>(want-copied,prefix_.size()-(size_t)offset);
      std::memcpy(out+copied,prefix_.data()+(size_t)offset,avail);
      copied+=(uint32_t)avail;
      continue;
    }
    const uint64_t block=ptvrange::block_for(offset);
    if(wanted_!=block){
      wanted_=block;suppressed_=ptvrange::kNone;
    }
    // Max three retained 2 MiB blocks. Evict on seek.
    for(auto it=blocks_.begin();it!=blocks_.end();){
      it=ptvrange::keep(it->first,wanted_)?std::next(it):blocks_.erase(it);
    }
    changed_.notify_all();
    const bool received=changed_.wait_for(lock,std::chrono::seconds(30),[&]{
      return stopping_||failed_||blocks_.count(block)>0;
    });
    if(!received||stopping_||failed_) {
      if(!received) {
        failed_=true;
        lock.unlock();
        ps4diag::note("VIDEO_IO","FAIL read-ahead block wait timeout");
        ps4diag::event(ptvtelemetry::Code::RemoteReadFailed,0,0,requests_);
      }
      return copied?int(copied):-1;
    }
    const auto it=blocks_.find(block);
    if(it==blocks_.end())return copied?int(copied):-1;
    const size_t local=(size_t)(offset%ptvrange::kBlock);
    if(local>=it->second.size())return copied?int(copied):-1;
    const size_t avail=std::min<size_t>(want-copied,it->second.size()-local);
    std::memcpy(out+copied,it->second.data()+local,avail);
    copied+=(uint32_t)avail;
  }
  return (int)copied;
}
void RangeStream::workerLoop() {
  std::unique_lock<std::mutex> lock(mutex_);
  while(!stopping_&&!failed_){
    std::set<uint64_t> available;
    for(const auto& entry:blocks_)available.insert(entry.first);
    const uint64_t totalBlocks=ptvrange::block_count(total_.load());
    const uint64_t block=ptvrange::choose(wanted_,totalBlocks,available,suppressed_);
    if(block==ptvrange::kNone){
      changed_.wait(lock);
      continue;
    }
    const uint64_t begin=block*ptvrange::kBlock;
    const uint64_t end=std::min<uint64_t>(total_.load()-1,
                                           begin+ptvrange::kBlock-1);
    const uint64_t size=total_.load();
    const std::string url=url_;
    lock.unlock();
    ps4net::Response response;
    bool ok=false;
    for(int retry=0;retry<2;retry++){
      try{
        response=ps4net::request(url,"GET",
          {{"Range","bytes="+std::to_string(begin)+"-"+std::to_string(end)}},
          "",(size_t)(end-begin)+2);
      }catch(const std::exception&){response.platform_error=-3;}
      catch(...){response.platform_error=-4;}
      uint64_t start=0,finish=0,count=0;
      ok=response.status==206&&response.platform_error==0&&
         !response.truncated&&
         ptvrange::parse_content_range(response.content_range,start,finish,count)&&
         start==begin&&finish==end&&count==size&&
         response.body.size()==end-begin+1;
      if(ok)break;
      if(retry==0)std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }
    lock.lock();
    if(stopping_)break;
    if(!ok){
      const bool demanded=block==wanted_;
      if(demanded)failed_=true;
      else suppressed_=block;
      changed_.notify_all();
      lock.unlock();
      ps4diag::note("VIDEO_IO","FAIL linear range block http="+
        std::to_string(response.status)+" native="+
        std::to_string(response.platform_error)+
        " demand="+std::to_string(demanded));
      if(demanded)ps4diag::event(ptvtelemetry::Code::RemoteReadFailed,
        response.status,response.platform_error,requests_.load());
      lock.lock();
      continue;
    }
    ++requests_;
    if(ptvrange::keep(block,wanted_)){
      blocks_[block]=std::vector<uint8_t>(response.body.begin(),response.body.end());
      const auto count=requests_.load();
      if(count==1||count%8==0) {
        lock.unlock();
        ps4diag::note("VIDEO_IO","PASS linear read-ahead ranges="+std::to_string(count));
        ps4diag::event(ptvtelemetry::Code::RemoteRangeFetched,206,0,count);
        lock.lock();
      }
    }
    changed_.notify_all();
  }
}
}
