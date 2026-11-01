#include "video_player.hpp"
#include "diagnostics.hpp"
#include "native_net.hpp"
#include "../src/nv12_scaler.hpp"
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
    ps4diag::note("VIDEO_IO","FAIL remote HEAD metadata");
    ps4diag::event(ptvtelemetry::Code::HeadFailed,head.status,head.platform_error);return false;
  }
  // Probe 64 KiB. Servers that ignore Range (HTTP 200) are not safe to
  // stream through shadPS4's buffering HTTP emulation.
  const uint64_t last=std::min(uint64_t(65535),head.content_length-1);
  const std::string range="bytes=0-"+std::to_string(last);
  const auto first=ps4net::request(url,"GET",{{"Range",range}},"",(size_t)last+2);
  uint64_t a=0,b=0,n=0;
  if(first.status!=206||first.platform_error!=0||
     !ptvrange::parse_content_range(first.content_range,a,b,n)||
     a!=0||b!=last||n!=head.content_length||
     first.truncated||first.body.size()!=last+1){
    error="CDN does not supply seekable HTTP 206 byte ranges";
    ps4diag::event(ptvtelemetry::Code::RangeProbeFailed,first.status,first.platform_error);
    ps4diag::note("VIDEO_IO","FAIL Range http="+std::to_string(first.status)+
      " transport="+std::to_string(first.platform_error)+
      " bytes="+std::to_string(first.body.size())+
      " range_ok="+std::to_string(!first.content_range.empty()));
    return false;
  }
  remoteReads_=0;
  if(!remote_.start(url,n,first.body,error)){
    ps4diag::note("VIDEO_IO","FAIL starting linear read-ahead worker");
    ps4diag::event(ptvtelemetry::Code::RemoteReadFailed);
    return false;
  }
  ps4diag::note("VIDEO_IO","PASS seekable media range, total bytes="+std::to_string(n));
  return true;
}
int Player::openRemote(void* ptr,const char* name){
  auto* self=static_cast<Player*>(ptr);
  const bool valid=self&&name&&std::strcmp(name,"/ptvremote.mp4")==0&&
         self->remote_.ready()&&self->remote_.size()>0;
  ps4diag::note("VIDEO_IO",valid?"PASS AvPlayer file opened":"FAIL AvPlayer file open");
  if(!valid)ps4diag::event(ptvtelemetry::Code::RemoteOpenFailed);
  return valid?0:-1;
}
int Player::closeRemote(void* ptr){
  return ptr?0:-1;
}
uint64_t Player::remoteSize(void* ptr){
  auto* self=static_cast<Player*>(ptr);
  return self?self->remote_.size():0;
}
int Player::readRemote(void* ptr,uint8_t* out,uint64_t position,uint32_t length){
  auto* self=static_cast<Player*>(ptr);
  if(!self||!out||length==0)return -1;
  const uint64_t calls=++self->remoteReads_;
  if(calls==1)ps4diag::note("VIDEO_IO","PASS first decoder read callback");
  // On this callback's small stack, never initiate native libSceHttp.
  return self->remote_.read(out,position,length);
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
    ps4diag::event(ptvtelemetry::Code::UnsupportedScheme);
    status_=Status::Failed;return false;
  }
  // Reject HLS playlists: shadPS4 AvPlayer currently does not implement
  // HLS. Downloading HLS as a regular file cannot produce playable video.
  if(isHls(url)){error="HLS playlist playback is unsupported by shadPS4";
    ps4diag::note("PLAYBACK","FAIL HLS format not supported by emulator");
    ps4diag::event(ptvtelemetry::Code::UnsupportedHls);
    status_=Status::Failed;return false;
  }
  const bool remote=url.rfind("https://",0)==0||url.rfind("http://",0)==0;
  if(remote && !prepareRemote(url,error)){status_=Status::Failed;return false;}
  // No URL or query string may be written to any logfile.
  int sys=sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER);
  if(sys<0){last_error_=sys;error="Cannot load PS4 hardware AvPlayer";
    ps4diag::note("PLAYBACK","FAIL module load="+std::to_string(sys));
    ps4diag::event(ptvtelemetry::Code::AvModuleFailed,0,sys);
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
    ps4diag::note("PLAYBACK","FAIL sceAvPlayerInit returned null");
    ps4diag::event(ptvtelemetry::Code::AvInitFailed);return false;}
  // The emulator cannot open arbitrary HTTPS URLs as PS4 mounted files.
  // The guest file callbacks above present a seekable remote MP4-compatible
  // byte stream to the emulator's FFmpeg demuxer.
  int ret=sceAvPlayerAddSource(handle_,remote?"/ptvremote.mp4":url.c_str());
  if(ret<0){last_error_=ret;error="PS4 decoder rejected stream source";
    ps4diag::note("PLAYBACK","FAIL AddSource code="+std::to_string(ret));
    ps4diag::event(ptvtelemetry::Code::AvSourceFailed,0,ret);
    sceAvPlayerClose(handle_);handle_=nullptr;status_=Status::Failed;return false;
  }
  ps4diag::note("PLAYBACK","PASS AvPlayer accepted video stream (URL excluded)");
  ps4diag::event(ptvtelemetry::Code::PlaybackStarted);
  status_=Status::Opening;
  frames_=0;
  totalConvertUs_=0;
  totalBlitUs_=0;
  maxConvertUs_=0;
  maxFrameGapMs_=stallCount_=0;
  lastDecodedAt_={};lastStallEvent_={};
  inputW_=inputH_=inputPitch_=visibleW_=visibleH_=0;
  fullRange_=false;
  audioBuffers_=0;
  audioFormatUnsupported_=false;
  ps4diag::playback_metrics({});
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
  if(sys<0){ps4diag::note("AUDIO","SKIP module load error="+std::to_string(sys));
    ps4diag::event(ptvtelemetry::Code::AudioModuleFailed,0,sys);return;}
  int ret=sceAudioOutInit();
  // 0x8026000E = ORBIS_AUDIO_OUT_ERROR_ALREADY_INIT.
  // shadPS4 and a real PS4 may have initialized AudioOut already.
  if(ret<0 && static_cast<uint32_t>(ret)!=0x8026000Eu){
    ps4diag::note("AUDIO","SKIP init error="+std::to_string(ret));
    ps4diag::event(ptvtelemetry::Code::AudioInitFailed,0,ret);return;
  }
  if(static_cast<uint32_t>(ret)==0x8026000Eu)
    ps4diag::note("AUDIO","INFO AudioOut already initialized; trying port");
  constexpr int samples=1024;
  const int port=sceAudioOutOpen(user_,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,
      samples,48000,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
  if(port<0){ps4diag::note("AUDIO","SKIP port error="+std::to_string(port));
    ps4diag::event(ptvtelemetry::Code::AudioPortFailed,0,port);return;}
  std::vector<uint8_t> output(samples*2*sizeof(int16_t),0);
  bool notified=false;
  while(audioRunning_){
    if(status_==Status::Paused){sceKernelUsleep(12000);continue;}
    AvFrame frame={};
    if(!handle_ || sceAvPlayerGetAudioData(handle_,&frame)==0 ||
       frame.data==nullptr){sceKernelUsleep(9000);continue;}
    if(frame.details.audio.channels!=2 ||
       frame.details.audio.sampleRate!=48000){
      audioFormatUnsupported_=true;
      if(!notified){ps4diag::note("AUDIO","SKIP stream format not 48k stereo PCM");
         ps4diag::event(ptvtelemetry::Code::AudioUnsupportedFormat);notified=true;}
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
       ps4diag::event(ptvtelemetry::Code::AudioOutputFailed,0,result);
      break;
    }
    ++audioBuffers_;
    if(!notified){ps4diag::note("AUDIO","PASS first audio buffer submitted");
       ps4diag::event(ptvtelemetry::Code::FirstAudioBuffer);notified=true;}
  }
  sceAudioOutClose(port);
}
void Player::publishMetrics(){
  ptvtelemetry::PlaybackMetrics metrics;
  metrics.decoder_width=(uint64_t)inputW_;
  metrics.decoder_height=(uint64_t)inputH_;
  metrics.pitch=(uint64_t)inputPitch_;
  metrics.visible_width=(uint64_t)visibleW_;
  metrics.visible_height=(uint64_t)visibleH_;
  metrics.output_width=(uint64_t)rgbW_;
  metrics.output_height=(uint64_t)rgbH_;
  metrics.full_range=fullRange_;
  metrics.frames=frames_;
  const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now()-startTime_).count();
  metrics.elapsed_ms=elapsed>0?(uint64_t)elapsed:0;
  if(metrics.elapsed_ms>0)
    metrics.effective_fps_x100=metrics.frames*100000ULL/metrics.elapsed_ms;
  if(frames_){
    metrics.avg_convert_us=totalConvertUs_/frames_;
    metrics.avg_blit_us=totalBlitUs_/frames_;
  }
  metrics.max_convert_us=maxConvertUs_;
  metrics.max_frame_gap_ms=maxFrameGapMs_;
  metrics.stall_count=stallCount_;
  metrics.range_requests=remote_.requests();
  metrics.decoder_read_calls=remoteReads_.load();
  metrics.audio_buffers=audioBuffers_.load();
  metrics.audio_format_unsupported=audioFormatUnsupported_.load();
  ps4diag::playback_metrics(metrics);
}
bool Player::render(Scene2D& scene){
  if(!handle_||status_==Status::Failed)return false;
  if(status_==Status::Paused){
    if(!rgbPixels_.empty())scene.BlitVideoRgb32(rgbPixels_.data(),rgbX_,rgbY_,rgbW_,rgbH_);
    return !rgbPixels_.empty();
  }
  // A decoder with no frames may still report Active. Never buffer forever.
  const int elapsed=bufferingSeconds();
  if(status_==Status::Opening){
    if(elapsed>=60){
      status_=Status::Failed;
      ps4diag::event(ptvtelemetry::Code::DecoderTimeout,0,0,remoteReads_.load());
       ps4diag::note("PLAYBACK","FAIL no frames in 60s; reads="+
        std::to_string(remoteReads_.load())+" CDN_ranges="+
        std::to_string(remote_.requests()));
      return false;
    }
    if(elapsed>=10&&elapsed/10!=lastProgressBucket_){
      lastProgressBucket_=elapsed/10;
      ps4diag::event(ptvtelemetry::Code::Buffering,0,0,(uint64_t)elapsed);
       ps4diag::note("PLAYBACK","BUFFERING seconds="+std::to_string(elapsed)+
        " reads="+std::to_string(remoteReads_.load())+
        " CDN_ranges="+std::to_string(remote_.requests()));
    }
  }
  const bool active=sceAvPlayerIsActive(handle_);
  if(!active){
    if(status_==Status::Playing){
      status_=Status::Ended;
      publishMetrics();
      ps4diag::event(ptvtelemetry::Code::PlaybackEnded,0,0,frames_);
      ps4diag::note("PLAYBACK","INFO video session became inactive after "+std::to_string(frames_)+" frames");
    }
    return false;
  }
  AvFrame frame={};
  const bool got=sceAvPlayerGetVideoDataEx(handle_,&frame);
  if(got==0||!frame.data){
    // Cached display frames conceal decoder stalls visually; report missing
    // fresh frames only during active playback, never during user pause.
    const auto now=std::chrono::steady_clock::now();
    if(frames_>0 && lastDecodedAt_!=std::chrono::steady_clock::time_point{}){
      const auto gap=std::chrono::duration_cast<std::chrono::milliseconds>(
          now-lastDecodedAt_).count();
      if(gap>=1500 && (lastStallEvent_==std::chrono::steady_clock::time_point{} ||
         now-lastStallEvent_>=std::chrono::seconds(5))){
        lastStallEvent_=now;
        ++stallCount_;
        maxFrameGapMs_=std::max(maxFrameGapMs_,(uint64_t)gap);
        publishMetrics();
        ps4diag::event(ptvtelemetry::Code::PlaybackStall,0,0,(uint64_t)gap);
      }
    }
    if(!rgbPixels_.empty())scene.BlitVideoRgb32(rgbPixels_.data(),rgbX_,rgbY_,rgbW_,rgbH_);
    return !rgbPixels_.empty();
  }
  const auto& v=frame.details.video;
  const int w=(int)v.width,h=(int)v.height;
  int pitch=(int)v.pitch;
  if(pitch<w)pitch=w;
  if(w<2||h<2||w>4096||h>2160||pitch>8192){
    if(!loggedInvalidFrame_){
      loggedInvalidFrame_=true;
      ps4diag::event(ptvtelemetry::Code::InvalidFrame,0,0,(uint64_t)w);
       ps4diag::note("PLAYBACK","FAIL invalid frame dimensions="+
        std::to_string(w)+"x"+std::to_string(h)+" pitch="+std::to_string(pitch));
    }
    return false;
  }
  // AvPlayer's decoder buffer may be height-aligned and cropped.
  // Use the actual plane height/pitch and visible crop, not an assumed
  // 2x2 block grid or the full padded picture.
  const int cropL=(int)v.cropLeft,cropR=(int)v.cropRight;
  const int cropT=(int)v.cropTop,cropB=(int)v.cropBottom;
  const int visibleW=w-cropL-cropR,visibleH=h-cropT-cropB;
  if(cropL<0||cropR<0||cropT<0||cropB<0||
     visibleW<2||visibleH<2){
    if(!loggedInvalidFrame_){
      loggedInvalidFrame_=true;
      ps4diag::note("PLAYBACK","FAIL invalid decoder crop offsets");
       ps4diag::event(ptvtelemetry::Code::InvalidCrop);
    }
    return false;
  }
  // Native 1080p output: full-HD originals are never downscaled to 720p.
  const float scale=std::min(1920.f/visibleW,1080.f/visibleH);
  const int outW=std::max(2,((int)(visibleW*scale)/2)*2);
  const int outH=std::max(2,((int)(visibleH*scale)/2)*2);
  if(outW>1920||outH>1080)return false;
  const auto convertStart=std::chrono::steady_clock::now();
  if(!ptvvideo::scale_nv12((const uint8_t*)frame.data,w,h,pitch,
                           cropL,cropT,visibleW,visibleH,outW,outH,
                           v.fullRange!=0,rgbPixels_)){
    if(!loggedInvalidFrame_){
      loggedInvalidFrame_=true;
      ps4diag::note("PLAYBACK","FAIL crop-aware NV12 conversion");
       ps4diag::event(ptvtelemetry::Code::ConversionFailed);
    }
    return false;
  }
  const auto convertEnd=std::chrono::steady_clock::now();
  if(lastDecodedAt_!=std::chrono::steady_clock::time_point{}){
    const auto gap=std::chrono::duration_cast<std::chrono::milliseconds>(
        convertEnd-lastDecodedAt_).count();
    if(gap>0)maxFrameGapMs_=std::max(maxFrameGapMs_,(uint64_t)gap);
  }
  lastDecodedAt_=convertEnd;
  inputW_=w;inputH_=h;inputPitch_=pitch;
  visibleW_=visibleW;visibleH_=visibleH;
  fullRange_=v.fullRange!=0;
  rgbW_=outW;rgbH_=outH;
  rgbX_=(1920-outW)/2;rgbY_=(1080-outH)/2;
  scene.BlitVideoRgb32(rgbPixels_.data(),rgbX_,rgbY_,outW,outH);
  const auto blitEnd=std::chrono::steady_clock::now();
  const auto convertUs=(uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
      convertEnd-convertStart).count();
  const auto blitUs=(uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
      blitEnd-convertEnd).count();
  totalConvertUs_+=convertUs;
  totalBlitUs_+=blitUs;
  maxConvertUs_=std::max(maxConvertUs_,convertUs);
  ++frames_;
  if(frames_==1||frames_%120==0){
    publishMetrics();
    const auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now()-startTime_).count();
    // Separate actual NV12 conversion cost from the VideoOut memory copy.
    // Running averages expose sustained CPU pressure without logging media.
    ps4diag::note("PLAYBACK","VIDEO_QUALITY size="+std::to_string(outW)+"x"+
      std::to_string(outH)+" bt709="+std::to_string(visibleH>=720)+
      " full_range="+std::to_string(v.fullRange!=0)+
      " convert_ms="+std::to_string(convertUs/1000)+
      " convert_us="+std::to_string(convertUs)+
      " avg_convert_us="+std::to_string(totalConvertUs_/frames_)+
      " max_convert_us="+std::to_string(maxConvertUs_)+
      " blit_us="+std::to_string(blitUs)+
      " avg_blit_us="+std::to_string(totalBlitUs_/frames_));
    ps4diag::event(ptvtelemetry::Code::PlaybackProgress,0,0,frames_);
     ps4diag::note("PLAYBACK","VIDEO_PROGRESS frames="+std::to_string(frames_)+
      " elapsed_s="+std::to_string(elapsed)+
      " ranges="+std::to_string(remote_.requests()));
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
     ps4diag::event(ptvtelemetry::Code::FirstFrame,0,0,frames_);
  }
  return true;
}
void Player::pauseToggle(){
  if(!handle_)return;
  if(status_==Status::Playing){
    int r=sceAvPlayerPause(handle_);
    if(r>=0){status_=Status::Paused;ps4diag::event(ptvtelemetry::Code::PlaybackPaused);
      ps4diag::note("PLAYBACK","PAUSE user control");}
  }else if(status_==Status::Paused){
    int r=sceAvPlayerResume(handle_);
    if(r>=0){status_=Status::Playing;lastDecodedAt_={};
      ps4diag::event(ptvtelemetry::Code::PlaybackResumed);
      ps4diag::note("PLAYBACK","RESUME user control");}
  }
}
void Player::close(){
  if(handle_){
    ps4diag::note("PLAYBACK","STOP decoded frames="+std::to_string(frames_));
  }
  audioRunning_=false;
  if(audioThread_.joinable())audioThread_.join();
  if(handle_){
    publishMetrics();
    ps4diag::event(ptvtelemetry::Code::PlaybackStopped,0,0,frames_);
    sceAvPlayerStop(handle_);sceAvPlayerClose(handle_);
    handle_=nullptr;
  }
  rgbPixels_.clear();rgbW_=rgbH_=0;
  remote_.stop();
  status_=Status::Idle;
}
}
