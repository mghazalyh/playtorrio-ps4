#include "video_player.hpp"
#include "diagnostics.hpp"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include <chrono>
#include <cctype>
#include <orbis/AudioOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include "graphics.h"

namespace ps4video {
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
  init.basePriority=160;
  init.videoBuffers=4;
  init.autoStart=1;
  init.defaultLanguage="en";
  handle_=sceAvPlayerInit(&init);
  if(!handle_){error="Cannot initialize PS4 video decoder";status_=Status::Failed;
    ps4diag::note("PLAYBACK","FAIL sceAvPlayerInit returned null");return false;}
  const bool hls=url.find(".m3u8")!=std::string::npos;
  int ret=hls?sceAvPlayerAddSourceEx(handle_,8,url.c_str()):
              sceAvPlayerAddSource(handle_,url.c_str());
  if(ret<0){last_error_=ret;error="PS4 decoder rejected stream source";
    ps4diag::note("PLAYBACK","FAIL AddSource code="+std::to_string(ret));
    sceAvPlayerClose(handle_);handle_=nullptr;status_=Status::Failed;return false;
  }
  ps4diag::note("PLAYBACK","PASS AvPlayer accepted video stream (URL excluded)");
  status_=Status::Opening;
  frames_=0;
  preferredTracks_=false;
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
  if(ret<0){ps4diag::note("AUDIO","SKIP init error="+std::to_string(ret));return;}
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
  if(!handle_||status_==Status::Failed||status_==Status::Paused)return false;
  int active=sceAvPlayerIsActive(handle_);
  if(!active){
    if(status_==Status::Opening &&
        std::chrono::steady_clock::now()-startTime_>std::chrono::seconds(35)){
      status_=Status::Failed;
      ps4diag::note("PLAYBACK","FAIL no decoded frames within 35 seconds");
    }
    if(status_==Status::Playing){
      status_=Status::Ended;
      ps4diag::note("PLAYBACK","INFO video session became inactive after "+std::to_string(frames_)+" frames");
    }
    return false;
  }
  AvFrame frame={};
  int got=sceAvPlayerGetVideoDataEx(handle_,&frame);
  if(got==0||!frame.data)return false;
  const auto& v=frame.details.video;
  const int w=(int)v.width,h=(int)v.height;
  int pitch=(int)v.pitch;
  if(pitch<w)pitch=w;
  if(w<2||h<2||w>4096||h>2160||pitch>8192)return false;
  const uint8_t* yPlane=(const uint8_t*)frame.data;
  const uint8_t* uvPlane=yPlane+(size_t)pitch*h;
  // Letterboxed video at up to 1152x648, using direct framebuffer 2x2
  // blocks. This is deliberately simple until a texture/GPU backend is ready.
  constexpr int maxW=1152,maxH=648;
  float ratio=std::min((float)maxW/w,(float)maxH/h);
  int outW=((int)(w*ratio)/2)*2,outH=((int)(h*ratio)/2)*2;
  if(outW<=0||outH<=0)return false;
  const int left=(1280-outW)/2,top=(720-outH)/2;
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
      scene.DrawRectangle(left+col,top+row,2,2,
         Color{clamp(r),clamp(g),clamp(b)});
    }
  }
  ++frames_;
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
  status_=Status::Idle;
}
}
