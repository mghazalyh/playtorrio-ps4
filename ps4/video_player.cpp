#include "video_player.hpp"
#include "diagnostics.hpp"
#include "native_net.hpp"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include <chrono>
#include <cctype>
#include <limits>
#include <cstdio>
#include <orbis/AudioOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include "graphics.h"

namespace ps4video {
namespace {
constexpr size_t kRemoteBlock=2*1024*1024;
// Strictly parse RFC 7233 Content-Range: "bytes start-end/total".
bool parseContentRange(const std::string& input,uint64_t& begin,
                       uint64_t& finish,uint64_t& total){
  const std::string prefix="bytes ";
  if(input.compare(0,prefix.size(),prefix)!=0)return false;
  size_t pos=prefix.size();
  auto number=[&](uint64_t& out)->bool{
    if(pos>=input.size()||input[pos]<'0'||input[pos]>'9')return false;
    uint64_t value=0;
    do{
      const int digit=input[pos]-'0';
      if(value>(std::numeric_limits<uint64_t>::max()-digit)/10)return false;
      value=value*10+digit;
      ++pos;
    }while(pos<input.size()&&input[pos]>='0'&&input[pos]<='9');
    out=value;
    return true;
  };
  if(!number(begin)||pos>=input.size()||input[pos++]!='-'||
     !number(finish)||pos>=input.size()||input[pos++]!='/'||
     !number(total)||pos!=input.size())return false;
  return total>0&&total<=(uint64_t)std::numeric_limits<int64_t>::max()&&
         begin<=finish&&finish<total;
}
bool isHls(const std::string& url){
  const auto until=url.substr(0,url.find('?'));
  return until.find(".m3u8")!=std::string::npos;
}
}

bool Player::prepareRemote(const std::string& url,std::string& error){
  // A short ranged probe both measures the file and verifies seeking.
  // Do not use a full GET: shadPS4's current HTTP backend buffers responses.
  const auto head=ps4net::request(url,"HEAD",{},"",0);
  ps4diag::note("VIDEO_IO","HEAD http="+std::to_string(head.status)+
     " transport="+std::to_string(head.platform_error)+
     " content_length="+std::to_string(head.has_content_length));
  if(head.status!=200||head.platform_error!=0||!head.has_content_length||
     head.content_length==0||head.content_length>(uint64_t)std::numeric_limits<int64_t>::max()){
    error="Stream does not provide a usable HTTPS HEAD length";
    ps4diag::note("VIDEO_IO","FAIL remote HEAD metadata");return false;
  }
  // Probe 64 KiB. Servers that ignore Range (HTTP 200) are not safe to
  // stream through shadPS4's buffering HTTP emulation.
  const uint64_t last=std::min(uint64_t(65535),head.content_length-1);
  const std::string range="bytes=0-"+std::to_string(last);
  const auto first=ps4net::request(url,"GET",{{"Range",range}},"",(size_t)last+2);
  uint64_t a=0,b=0,n=0;
  if(first.status!=206||first.platform_error!=0||
     !parseContentRange(first.content_range,a,b,n)||
     a!=0||b!=last||n!=head.content_length||
     first.truncated||first.body.size()!=last+1){
    error="CDN does not supply seekable HTTP 206 byte ranges";
    ps4diag::note("VIDEO_IO","FAIL Range http="+std::to_string(first.status)+
      " transport="+std::to_string(first.platform_error)+
      " bytes="+std::to_string(first.body.size())+
      " range_ok="+std::to_string(!first.content_range.empty()));
    return false;
  }
  remoteUrl_=url;
  remoteBytes_=n;
  cacheOffset_=0;
  cache_.assign(first.body.begin(),first.body.end());
  readFailureLogged_=false;
  remoteReads_=0;
  rangeRequests_=0;
  ps4diag::note("VIDEO_IO","PASS seekable media range, total bytes="+std::to_string(n));
  return true;
}
int Player::openRemote(void* ptr,const char* name){
  auto* self=static_cast<Player*>(ptr);
  const bool valid=self&&name&&std::strcmp(name,"/ptvremote.mp4")==0&&
         !self->remoteUrl_.empty()&&self->remoteBytes_>0;
  ps4diag::note("VIDEO_IO",valid?"PASS AvPlayer file opened":"FAIL AvPlayer file open");
  return valid?0:-1;
}
int Player::closeRemote(void* ptr){
  return ptr?0:-1;
}
uint64_t Player::remoteSize(void* ptr){
  auto* self=static_cast<Player*>(ptr);
  return self?self->remoteBytes_:0;
}
int Player::readRemote(void* ptr,uint8_t* out,uint64_t position,uint32_t length){
  auto* self=static_cast<Player*>(ptr);
  if(!self||!out||length==0||self->remoteUrl_.empty())return -1;
  const uint64_t calls=++self->remoteReads_;
  if(calls==1)ps4diag::note("VIDEO_IO","PASS first decoder read callback");
  std::lock_guard<std::mutex> lock(self->remoteMutex_);
  if(position>=self->remoteBytes_)return 0;
  const uint32_t want=(uint32_t)std::min(uint64_t(length),self->remoteBytes_-position);
  uint32_t copied=0;
  while(copied<want){
    const uint64_t offset=position+copied;
    if(self->cache_.empty()||offset<self->cacheOffset_||
       offset-self->cacheOffset_>=self->cache_.size()){
      const uint64_t start=(offset/kRemoteBlock)*kRemoteBlock;
      const uint64_t end=std::min(self->remoteBytes_-1,start+kRemoteBlock-1);
      auto response=ps4net::request(self->remoteUrl_,"GET",
          {{"Range","bytes="+std::to_string(start)+"-"+std::to_string(end)}},
          "",(size_t)(end-start)+2);
      uint64_t a=0,b=0,n=0;
      if(response.status!=206||response.platform_error!=0||
         response.truncated||!parseContentRange(response.content_range,a,b,n)||
         a!=start||b!=end||n!=self->remoteBytes_||
         response.body.size()!=end-start+1){
        if(!self->readFailureLogged_){
          ps4diag::note("VIDEO_IO","FAIL remote read http="+
            std::to_string(response.status)+" transport="+
            std::to_string(response.platform_error)+" errno="+
            std::to_string(response.last_errno));
          self->readFailureLogged_=true;
        }
        return copied?int(copied):-1;
      }
      self->cacheOffset_=start;
      self->cache_.assign(response.body.begin(),response.body.end());
      const uint64_t count=++self->rangeRequests_;
      if(count==1||count%8==0)ps4diag::note("VIDEO_IO","PASS CDN range chunks="+std::to_string(count));
    }
    const size_t into=(size_t)(offset-self->cacheOffset_);
    const uint32_t available=std::min((size_t)(want-copied),self->cache_.size()-into);
    std::memcpy(out+copied,self->cache_.data()+into,available);
    copied+=available;
  }
  return (int)copied;
}
void* Player::allocate(void*,uint32_t alignment,uint32_t size){
  if(alignment<sizeof(void*))alignment=sizeof(void*);
  if((alignment&(alignment-1))!=0)return nullptr;
  void* ptr=nullptr;
  return posix_memalign(&ptr,alignment,size)==0?ptr:nullptr;
}
void Player::deallocate(void*,void* mem){free(mem);}
bool Player::start(const std::string& url,std::string& error){
  close();
  if(url.rfind("https://",0)!=0&&url.rfind("http://",0)!=0&&url.rfind("/data/",0)!=0){
    error="Only HTTP(S) or /data/ video sources are supported";
    ps4diag::note("PLAYBACK","FAIL unsupported URL scheme (URL hidden)");
    status_=Status::Failed;return false;
  }
  // Reject HLS playlists: shadPS4 AvPlayer currently does not implement
  // HLS. Downloading HLS as a regular file cannot produce playable video.
  if(isHls(url)){error="HLS playlist playback is unsupported by shadPS4";
    ps4diag::note("PLAYBACK","FAIL HLS format not supported by emulator");
    status_=Status::Failed;return false;
  }
  const bool remote=url.rfind("https://",0)==0||url.rfind("http://",0)==0;
  if(remote && !prepareRemote(url,error)){status_=Status::Failed;return false;}
  // No URL or query string may be written to any logfile.
  int sys=sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER);
  if(sys<0){last_error_=sys;error="Cannot load PS4 hardware AvPlayer";
    ps4diag::note("PLAYBACK","FAIL module load="+std::to_string(sys));
    status_=Status::Failed;return false;
  }
  AvInitData init={};
  init.alloc.allocate=&Player::allocate;
  init.alloc.deallocate=&Player::deallocate;
  init.alloc.allocateTexture=&Player::allocate;
  init.alloc.deallocateTexture=&Player::deallocate;
  if(remote){
    init.file.object=this;
    init.file.open=&Player::openRemote;
    init.file.close=&Player::closeRemote;
    init.file.readOffset=&Player::readRemote;
    init.file.size=&Player::remoteSize;
  }
  init.basePriority=160;
  init.videoBuffers=4;
  init.autoStart=1;
  init.defaultLanguage="en";
  handle_=sceAvPlayerInit(&init);
  if(!handle_){error="Cannot initialize PS4 video decoder";status_=Status::Failed;
    ps4diag::note("PLAYBACK","FAIL sceAvPlayerInit returned null");return false;}
  // The emulator cannot open arbitrary HTTPS URLs as PS4 mounted files.
  // The guest file callbacks above present a seekable remote MP4-compatible
  // byte stream to the emulator's FFmpeg demuxer.
  int ret=sceAvPlayerAddSource(handle_,remote?"/ptvremote.mp4":url.c_str());
  if(ret<0){last_error_=ret;error="PS4 decoder rejected stream source";
    ps4diag::note("PLAYBACK","FAIL AddSource code="+std::to_string(ret));
    sceAvPlayerClose(handle_);handle_=nullptr;status_=Status::Failed;return false;
  }
  ps4diag::note("PLAYBACK","PASS AvPlayer accepted video stream (URL excluded)");
  status_=Status::Opening;
  frames_=0;
  preferredTracks_=false;
  lastProgressBucket_=-1;
  loggedInvalidFrame_=false;
  startTime_=std::chrono::steady_clock::now();
  sceUserServiceGetInitialUser(&user_);
  audioRunning_=true;
  audioThread_=std::thread([this](){audioLoop();});
  return true;
}
void Player::audioLoop(){
  // PS4 AvPlayer delivers decoded audio separately; output signed 16-bit
  // stereo 48kHz if the incoming audio matches the hardware port.
  int sys=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT);
  if(sys<0){ps4diag::note("AUDIO","SKIP module load error="+std::to_string(sys));return;}
  int ret=sceAudioOutInit();
  // 0x8026000E = ORBIS_AUDIO_OUT_ERROR_ALREADY_INIT.
  // shadPS4 and a real PS4 may have initialized AudioOut already.
  if(ret<0 && static_cast<uint32_t>(ret)!=0x8026000Eu){
    ps4diag::note("AUDIO","SKIP init error="+std::to_string(ret));return;
  }
  if(static_cast<uint32_t>(ret)==0x8026000Eu)
    ps4diag::note("AUDIO","INFO AudioOut already initialized; trying port");
  constexpr int samples=1024;
  const int port=sceAudioOutOpen(user_,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,
      samples,48000,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
  if(port<0){ps4diag::note("AUDIO","SKIP port error="+std::to_string(port));return;}
  std::vector<uint8_t> output(samples*2*sizeof(int16_t),0);
  bool notified=false;
  while(audioRunning_){
    if(status_==Status::Paused){sceKernelUsleep(12000);continue;}
    AvFrame frame={};
    if(!handle_ || sceAvPlayerGetAudioData(handle_,&frame)==0 ||
       frame.data==nullptr){sceKernelUsleep(9000);continue;}
    if(frame.details.audio.channels!=2 ||
       frame.details.audio.sampleRate!=48000){
      if(!notified){ps4diag::note("AUDIO","SKIP stream format not 48k stereo PCM");notified=true;}
      sceKernelUsleep(8000);continue;
    }
    // Copy no more than the declared returned buffer size.
    const size_t available=std::min((size_t)frame.details.audio.size,output.size());
    if(!available){sceKernelUsleep(8000);continue;}
    std::memset(output.data(),0,output.size());
    std::memcpy(output.data(),frame.data,available);
    int result=sceAudioOutOutput(port,output.data());
    if(result<0){
      ps4diag::note("AUDIO","FAIL sample output code="+std::to_string(result));
      break;
    }
    if(!notified){ps4diag::note("AUDIO","PASS first audio buffer submitted");notified=true;}
  }
  sceAudioOutClose(port);
}
bool Player::render(Scene2D& scene){
  if(!handle_||status_==Status::Failed)return false;
  if(status_==Status::Paused){
    if(!rgbPixels_.empty())scene.BlitRgb32(rgbPixels_.data(),rgbX_,rgbY_,rgbW_,rgbH_);
    return !rgbPixels_.empty();
  }
  // A decoder with no frames may still report Active. Never buffer forever.
  const int elapsed=bufferingSeconds();
  if(status_==Status::Opening){
    if(elapsed>=60){
      status_=Status::Failed;
      ps4diag::note("PLAYBACK","FAIL no frames in 60s; reads="+
        std::to_string(remoteReads_.load())+" CDN_ranges="+
        std::to_string(rangeRequests_.load()));
      return false;
    }
    if(elapsed>=10&&elapsed/10!=lastProgressBucket_){
      lastProgressBucket_=elapsed/10;
      ps4diag::note("PLAYBACK","BUFFERING seconds="+std::to_string(elapsed)+
        " reads="+std::to_string(remoteReads_.load())+
        " CDN_ranges="+std::to_string(rangeRequests_.load()));
    }
  }
  const bool active=sceAvPlayerIsActive(handle_);
  if(!active){
    if(status_==Status::Playing){
      status_=Status::Ended;
      ps4diag::note("PLAYBACK","INFO video session became inactive after "+std::to_string(frames_)+" frames");
    }
    return false;
  }
  AvFrame frame={};
  const bool got=sceAvPlayerGetVideoDataEx(handle_,&frame);
  if(got==0||!frame.data){
    if(!rgbPixels_.empty())scene.BlitRgb32(rgbPixels_.data(),rgbX_,rgbY_,rgbW_,rgbH_);
    return !rgbPixels_.empty();
  }
  const auto& v=frame.details.video;
  const int w=(int)v.width,h=(int)v.height;
  int pitch=(int)v.pitch;
  if(pitch<w)pitch=w;
  if(w<2||h<2||w>4096||h>2160||pitch>8192){
    if(!loggedInvalidFrame_){
      loggedInvalidFrame_=true;
      ps4diag::note("PLAYBACK","FAIL invalid frame dimensions="+
        std::to_string(w)+"x"+std::to_string(h)+" pitch="+std::to_string(pitch));
    }
    return false;
  }
  const uint8_t* yPlane=(const uint8_t*)frame.data;
  const uint8_t* uvPlane=yPlane+(size_t)pitch*h;
  // Letterboxed video at up to 1152x648, using direct framebuffer 2x2
  // blocks. This is deliberately simple until a texture/GPU backend is ready.
  constexpr int maxW=960,maxH=540;
  float ratio=std::min((float)maxW/w,(float)maxH/h);
  int outW=((int)(w*ratio)/2)*2,outH=((int)(h*ratio)/2)*2;
  if(outW<=0||outH<=0)return false;
  const int left=(1280-outW)/2,top=(720-outH)/2;
  rgbPixels_.resize((size_t)outW*outH);
  for(int row=0;row<outH;row+=2) {
    const int sy=std::min(h-1,(int)(((float)row/outH)*h));
    const uint8_t* luma=yPlane+(size_t)sy*pitch;
    const uint8_t* chroma=uvPlane+(size_t)(sy/2)*pitch;
    for(int col=0;col<outW;col+=2){
      int sx=std::min(w-2,(int)(((float)col/outW)*w))&~1;
      int Y=(int)luma[sx]-16;
      int U=(int)chroma[sx]-128,V=(int)chroma[sx+1]-128;
      int r=(298*Y+409*V+128)>>8;
      int g=(298*Y-100*U-208*V+128)>>8;
      int b=(298*Y+516*U+128)>>8;
      auto clamp=[](int c)->uint8_t{return (uint8_t)std::max(0,std::min(255,c));};
      const uint32_t pixel=0x80000000u |
           ((uint32_t)clamp(r)<<16)|((uint32_t)clamp(g)<<8)|clamp(b);
      const size_t at=(size_t)row*outW+col;
      rgbPixels_[at]=pixel;rgbPixels_[at+1]=pixel;
      rgbPixels_[at+outW]=pixel;rgbPixels_[at+outW+1]=pixel;
    }
  }
  rgbW_=outW;rgbH_=outH;rgbX_=left;rgbY_=top;
  scene.BlitRgb32(rgbPixels_.data(),rgbX_,rgbY_,rgbW_,rgbH_);
  ++frames_;
  if(frames_==1||frames_%120==0){
    const auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now()-startTime_).count();
    ps4diag::note("PLAYBACK","VIDEO_PROGRESS frames="+std::to_string(frames_)+
      " elapsed_s="+std::to_string(elapsed)+
      " ranges="+std::to_string(rangeRequests_.load()));
  }
  if(!preferredTracks_){
    preferredTracks_=true;
    // Prefer language metadata only when explicitly English; otherwise
    // leave the decoder's default selection untouched.
    int n=sceAvPlayerStreamCount(handle_);
    int englishAudio=-1,englishText=-1;
    if(n>0&&n<80){
      for(int i=0;i<n;i++){
        AvStreamInfo info={};
        if(sceAvPlayerGetStreamInfo(handle_,i,&info)<0)continue;
        const uint8_t* lang=info.type==1?info.details.audio.language:
                           info.type==2?info.details.video.language:nullptr;
        if(!lang)continue;
        bool en=(std::tolower(lang[0])=='e'&&
                 std::tolower(lang[1])=='n');
        if(en&&info.type==1&&englishAudio<0)englishAudio=i;
        if(en&&info.type==2&&englishText<0)englishText=i;
      }
      if(englishAudio>=0){
        for(int i=0;i<n;i++){
          AvStreamInfo info={};
          if(sceAvPlayerGetStreamInfo(handle_,i,&info)<0||info.type!=1)continue;
          if(i==englishAudio)sceAvPlayerEnableStream(handle_,i);
          else sceAvPlayerDisableStream(handle_,i);
        }
        ps4diag::note("AUDIO","PASS selected embedded English-labeled stream");
      }else ps4diag::note("AUDIO","INFO no English-labeled stream; retaining default");
      if(englishText>=0){
        sceAvPlayerEnableStream(handle_,englishText);
        ps4diag::note("SUBTITLES","INFO enabled English embedded text stream; rendering not guaranteed");
      }else ps4diag::note("SUBTITLES","INFO no English embedded text stream enumerated");
    }
  }
  if(status_==Status::Opening){
    status_=Status::Playing;
    ps4diag::note("PLAYBACK","PASS first decoded video frame rendered");
  }
  return true;
}
void Player::pauseToggle(){
  if(!handle_)return;
  if(status_==Status::Playing){
    int r=sceAvPlayerPause(handle_);
    if(r>=0){status_=Status::Paused;ps4diag::note("PLAYBACK","PAUSE user control");}
  }else if(status_==Status::Paused){
    int r=sceAvPlayerResume(handle_);
    if(r>=0){status_=Status::Playing;ps4diag::note("PLAYBACK","RESUME user control");}
  }
}
void Player::close(){
  if(handle_){
    ps4diag::note("PLAYBACK","STOP decoded frames="+std::to_string(frames_));
  }
  audioRunning_=false;
  if(audioThread_.joinable())audioThread_.join();
  if(handle_){
    sceAvPlayerStop(handle_);sceAvPlayerClose(handle_);
    handle_=nullptr;
  }
  remoteUrl_.clear();
  remoteBytes_=0;
  cache_.clear();
  cacheOffset_=0;
  status_=Status::Idle;
}
}
