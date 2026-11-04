#include "video_player.hpp"
#include "diagnostics.hpp"
#include "native_net.hpp"
#include "../src/nv12_scaler.hpp"
#include "../src/audio_pcm.hpp"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include <chrono>
#include <cctype>
#include <limits>
#include <cstdio>
#include <exception>
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
  status_=Status::Opening;
  frames_=0;totalConvertUs_=0;totalBlitUs_=0;maxConvertUs_=0;
  maxFrameGapMs_=0;stallCount_=0;videoEmptyPolls_=0;
  videoInvalidFrames_=0;videoConversionFailures_=0;
  videoPresentedFrames_=0;videoCachedRedraws_=0;videoSkippedFrames_=0;
  audioEmptyPolls_=0;audioConvertedChunks_=0;
  audioOutputFailures_=0;audioInvalidChunks_=0;
  lastDecodedNs_=0;lastStallEvent_={};
  {
    std::lock_guard<std::mutex> guard(frameMutex_);
    for(auto& f:rgbFrames_)f.clear();
    frontIndex_=0;backIndex_=1;rgbW_=rgbH_=rgbX_=rgbY_=0;
    inputW_=inputH_=inputPitch_=visibleW_=visibleH_=0;
    fullRange_=false;frameVersion_=0;
  }
  lastDisplayedVersion_=0;lastProgressFrameBucket_=0;lastMetricsAt_={};
  audioBuffers_=0;audioChannels_=0;audioSampleRateHz_=0;
  audioFormatUnsupported_=false;preferredTracks_=false;
  lastProgressBucket_=-1;loggedInvalidFrame_=false;
  startTime_=std::chrono::steady_clock::now();
  ps4diag::event(ptvtelemetry::Code::PlaybackStarted);
  ps4diag::playback_metrics({});
  sceUserServiceGetInitialUser(&user_);
  audioRunning_=true;videoRunning_=true;
  try {
    audioThread_=std::thread([this](){audioLoop();});
    videoThread_=std::thread([this](){videoLoop();});
  }catch(const std::exception&){
    videoRunning_=false;audioRunning_=false;
    ps4diag::event(ptvtelemetry::Code::VideoWorkerFailed);
    ps4diag::note("VIDEO_THREAD","FAIL video worker initialization");
    close();status_=Status::Failed;
    error="Could not start native video worker";return false;
  }
  ps4diag::event(ptvtelemetry::Code::VideoWorkerStarted);
  ps4diag::note("VIDEO_THREAD","PASS dedicated decoder and NV12 conversion thread");
  return true;

}
void Player::audioLoop(){
  // PS4 AvPlayer yields interleaved PCM16. Preserve every grain, and adapt
  // mono, multichannel front-pair and 44.1kHz to the fixed 48kHz stereo port.
  const int sys=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT);
  if(sys<0){ps4diag::event(ptvtelemetry::Code::AudioModuleFailed,0,sys);
    ps4diag::note("AUDIO","FAIL module="+std::to_string(sys));return;}
  const int init=sceAudioOutInit();
  if(init<0&&static_cast<uint32_t>(init)!=0x8026000Eu){
    ps4diag::event(ptvtelemetry::Code::AudioInitFailed,0,init);return;}
  constexpr size_t grain=1024;
  const int port=sceAudioOutOpen(user_,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,
      (int)grain,48000,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
  if(port<0){ps4diag::event(ptvtelemetry::Code::AudioPortFailed,0,port);
    ps4diag::note("AUDIO","FAIL port="+std::to_string(port));return;}
  ps4diag::event(ptvtelemetry::Code::AudioPortReady);
  ptvaudio::ResampleState resampler;
  std::vector<int16_t> pending,output(grain*2);
  bool reportedInvalid=false,reportedConversion=false,firstOutput=false;
  while(audioRunning_.load()){
    if(status_==Status::Paused){sceKernelUsleep(12000);continue;}
    AvFrame frame={};
    if(!handle_||!sceAvPlayerGetAudioData(handle_,&frame)||!frame.data){
      ++audioEmptyPolls_;sceKernelUsleep(4000);continue;
    }
    const unsigned channels=frame.details.audio.channels;
    const unsigned sampleRate=frame.details.audio.sampleRate;
    audioChannels_=channels>=1&&channels<=16?channels:0;
    audioSampleRateHz_=sampleRate>=8000&&sampleRate<=384000?sampleRate:0;
    if(!ptvaudio::append_pcm48_stereo(
        static_cast<const uint8_t*>(frame.data),
        frame.details.audio.size,channels,sampleRate,resampler,pending)){
      ++audioInvalidChunks_;audioFormatUnsupported_=true;
      if(!reportedInvalid){reportedInvalid=true;
        ps4diag::event(ptvtelemetry::Code::AudioUnsupportedFormat,0,0,channels);
        ps4diag::note("AUDIO","FAIL PCM16 input geometry/queue limit");}
      sceKernelUsleep(3000);continue;
    }
    if(channels!=2||sampleRate!=48000){
      ++audioConvertedChunks_;
      if(!reportedConversion){reportedConversion=true;
        ps4diag::event(ptvtelemetry::Code::AudioConverted);
        ps4diag::note("AUDIO","INFO 48k stereo PCM16 resample/front-pair mapping");}
    }
    size_t consumed=0;
    while(audioRunning_.load()&&pending.size()-consumed>=grain*2){
      std::memcpy(output.data(),pending.data()+consumed,grain*2*sizeof(int16_t));
      const int rc=sceAudioOutOutput(port,output.data());
      if(rc<0){++audioOutputFailures_;
        ps4diag::event(ptvtelemetry::Code::AudioOutputFailed,0,rc);
        ps4diag::note("AUDIO","FAIL AudioOut="+std::to_string(rc));
        audioRunning_=false;break;}
      consumed+=grain*2;++audioBuffers_;
      if(!firstOutput){firstOutput=true;
        ps4diag::event(ptvtelemetry::Code::FirstAudioBuffer);
        ps4diag::note("AUDIO","PASS first 1024-frame AudioOut grain");}
    }
    if(consumed)pending.erase(pending.begin(),pending.begin()+consumed);
  }
  sceAudioOutClose(port);
}
void Player::videoLoop(){
  // Stremio-PS4 inspired ownership: only this thread touches decoded
  // AvPlayer video frames. CPU conversion occurs before asking for the next
  // decoder frame; the UI merely blits the completed front buffer.
  auto previous=std::chrono::steady_clock::time_point{};
  while(videoRunning_.load()){
    if(status_==Status::Paused){previous={};sceKernelUsleep(8000);continue;}
    AvFrame frame={};
    if(!handle_||!sceAvPlayerGetVideoDataEx(handle_,&frame)||!frame.data){
      ++videoEmptyPolls_;sceKernelUsleep(2000);continue;
    }
    const auto& v=frame.details.video;
    const int w=(int)v.width,h=(int)v.height;
    const int pitch=std::max(w,(int)v.pitch);
    const int cropL=(int)v.cropLeft,cropR=(int)v.cropRight;
    const int cropT=(int)v.cropTop,cropB=(int)v.cropBottom;
    const int vw=w-cropL-cropR,vh=h-cropT-cropB;
    if(w<2||h<2||w>4096||h>2160||pitch>8192||
       cropL<0||cropR<0||cropT<0||cropB<0||vw<2||vh<2){
      ++videoInvalidFrames_;
      if(!loggedInvalidFrame_){loggedInvalidFrame_=true;
        ps4diag::event(ptvtelemetry::Code::InvalidFrame,0,0,(uint64_t)w);
        ps4diag::note("VIDEO_THREAD","FAIL invalid decoded frame/crop");}
      continue;
    }
    const float scale=std::min(1920.f/vw,1080.f/vh);
    const int outW=std::max(2,((int)(vw*scale)/2)*2);
    const int outH=std::max(2,((int)(vh*scale)/2)*2);
    if(outW>1920||outH>1080){++videoInvalidFrames_;continue;}
    const auto began=std::chrono::steady_clock::now();
    bool converted=false;
    try{
      converted=ptvvideo::scale_nv12(static_cast<const uint8_t*>(frame.data),
         w,h,pitch,cropL,cropT,vw,vh,outW,outH,v.fullRange!=0,
         rgbFrames_[backIndex_]);
    }catch(const std::exception&){
      ps4diag::event(ptvtelemetry::Code::ConversionFailed);
      ps4diag::note("VIDEO_THREAD","FAIL frame memory allocation");
      videoRunning_=false;status_=Status::Failed;return;
    }
    if(!converted){++videoConversionFailures_;
      if(!loggedInvalidFrame_){loggedInvalidFrame_=true;
        ps4diag::event(ptvtelemetry::Code::ConversionFailed);
        ps4diag::note("VIDEO_THREAD","FAIL NV12 conversion");}
      continue;
    }
    const auto now=std::chrono::steady_clock::now();
    const auto us=(uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
      now-began).count();
    totalConvertUs_+=us;
    auto m=maxConvertUs_.load();
    while(us>m&&!maxConvertUs_.compare_exchange_weak(m,us)){}
    if(previous!=std::chrono::steady_clock::time_point{}){
      const auto gap=std::chrono::duration_cast<std::chrono::milliseconds>(
        now-previous).count();
      if(gap>0){auto maxGap=maxFrameGapMs_.load();
        while((uint64_t)gap>maxGap&&
           !maxFrameGapMs_.compare_exchange_weak(maxGap,(uint64_t)gap)){}}
    }
    previous=now;
    {
      std::lock_guard<std::mutex> guard(frameMutex_);
      std::swap(frontIndex_,backIndex_);
      inputW_=w;inputH_=h;inputPitch_=pitch;
      visibleW_=vw;visibleH_=vh;fullRange_=v.fullRange!=0;
      rgbW_=outW;rgbH_=outH;rgbX_=(1920-outW)/2;rgbY_=(1080-outH)/2;
      ++frameVersion_;
    }
    lastDecodedNs_=std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    const auto n=++frames_;
    if(n==1||n%120==0)
      ps4diag::note("VIDEO_THREAD","VIDEO_QUALITY worker=1 frames="+
        std::to_string(n)+" decoder="+std::to_string(w)+"x"+std::to_string(h)+
        " output="+std::to_string(outW)+"x"+std::to_string(outH)+
        " native_sse2="+std::to_string((outW==vw&&outH==vh&&
             ptvvideo::native_sse2_enabled())?1:0)+
        " full_range="+std::to_string(v.fullRange!=0)+
        " convert_us="+std::to_string(us)+
        " avg_convert_us="+std::to_string(totalConvertUs_.load()/n));
  }
}
void Player::publishMetrics(){
  ptvtelemetry::PlaybackMetrics m;
  {
    std::lock_guard<std::mutex> guard(frameMutex_);
    m.decoder_width=inputW_;m.decoder_height=inputH_;m.pitch=inputPitch_;
    m.visible_width=visibleW_;m.visible_height=visibleH_;
    m.output_width=rgbW_;m.output_height=rgbH_;m.full_range=fullRange_;
  }
  m.frames=frames_.load();
  const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now()-startTime_).count();
  m.elapsed_ms=elapsed>0?(uint64_t)elapsed:0;
  if(m.elapsed_ms)m.effective_fps_x100=m.frames*100000ULL/m.elapsed_ms;
  if(m.frames)m.avg_convert_us=totalConvertUs_.load()/m.frames;
  if(videoPresentedFrames_)m.avg_blit_us=
       totalBlitUs_.load()/videoPresentedFrames_.load();
  m.max_convert_us=maxConvertUs_.load();
  m.max_frame_gap_ms=maxFrameGapMs_.load();
  m.stall_count=stallCount_.load();
  m.range_requests=remote_.requests();
  m.range_downloaded_bytes=remote_.downloaded_bytes();
  m.range_retry_attempts=remote_.retry_attempts();
  m.range_read_timeouts=remote_.read_timeouts();
  m.range_rejected_blocks=remote_.rejected_ranges();
  m.decoder_read_calls=remoteReads_.load();
  m.audio_buffers=audioBuffers_.load();
  m.audio_channels=audioChannels_.load();
  m.audio_sample_rate_hz=audioSampleRateHz_.load();
  m.audio_format_unsupported=audioFormatUnsupported_.load();
  m.video_worker_enabled=true;
  m.video_empty_polls=videoEmptyPolls_.load();
  m.video_invalid_frames=videoInvalidFrames_.load();
  m.video_conversion_failures=videoConversionFailures_.load();
  m.video_presented_frames=videoPresentedFrames_.load();
  m.video_cached_redraws=videoCachedRedraws_.load();
  m.video_skipped_frames=videoSkippedFrames_.load();
  if(m.elapsed_ms>0)
    m.presented_fps_x100=videoPresentedFrames_.load()*100000ULL/m.elapsed_ms;
  m.audio_empty_polls=audioEmptyPolls_.load();
  m.audio_converted_chunks=audioConvertedChunks_.load();
  m.audio_output_failures=audioOutputFailures_.load();
  m.audio_invalid_chunks=audioInvalidChunks_.load();
  ps4diag::playback_metrics(m);
}
bool Player::render(Scene2D& scene){
  if(!handle_||status_==Status::Failed)return false;
  // Five-second independent snapshots are uploaded by TorriODB's throttled
  // worker even if decoding stalls or no new 120-frame progress event arrives.
  const auto snapshotNow=std::chrono::steady_clock::now();
  if(lastMetricsAt_==std::chrono::steady_clock::time_point{}||
     snapshotNow-lastMetricsAt_>=std::chrono::seconds(5)){
    lastMetricsAt_=snapshotNow;
    publishMetrics();
  }
  const int elapsed=bufferingSeconds();
  if(status_==Status::Opening){
    if(elapsed>=60){publishMetrics();status_=Status::Failed;
      ps4diag::event(ptvtelemetry::Code::DecoderTimeout,0,0,remoteReads_.load());
      ps4diag::note("PLAYBACK","FAIL decoder timeout reads="+
        std::to_string(remoteReads_.load())+" ranges="+
        std::to_string(remote_.requests()));return false;}
    if(elapsed>=10&&elapsed/10!=lastProgressBucket_){
      lastProgressBucket_=elapsed/10;publishMetrics();
      ps4diag::event(ptvtelemetry::Code::Buffering,0,0,(uint64_t)elapsed);
      ps4diag::note("PLAYBACK","BUFFERING seconds="+std::to_string(elapsed)+
        " video_empty_polls="+std::to_string(videoEmptyPolls_.load())+
        " reads="+std::to_string(remoteReads_.load()));}
  }
  if(!sceAvPlayerIsActive(handle_)&&status_==Status::Playing){
    status_=Status::Ended;publishMetrics();
    ps4diag::event(ptvtelemetry::Code::PlaybackEnded,0,0,frames_.load());
    ps4diag::note("PLAYBACK","INFO stream ended frames="+
      std::to_string(frames_.load()));
  }
  bool ready=false,updated=false;
  uint64_t version=0;
  const auto start=std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> guard(frameMutex_);
    if(!rgbFrames_[frontIndex_].empty()&&rgbW_>0&&rgbH_>0){
      scene.BlitVideoRgb32(rgbFrames_[frontIndex_].data(),
                           rgbX_,rgbY_,rgbW_,rgbH_);
      ready=true;version=frameVersion_.load();
      updated=version!=lastDisplayedVersion_;
    }
  }
  if(ready){
    totalBlitUs_+=(uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-start).count();
    if(updated){
      ++videoPresentedFrames_;
      if(lastDisplayedVersion_&&version>lastDisplayedVersion_+1)
        videoSkippedFrames_+=version-lastDisplayedVersion_-1;
      lastDisplayedVersion_=version;
    } else ++videoCachedRedraws_;
  }
  if(status_==Status::Paused)return ready;
  const int64_t last=lastDecodedNs_.load();
  if(status_==Status::Playing&&last>0){
    const auto now=std::chrono::steady_clock::now();
    const auto nanos=std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    const uint64_t gap=nanos>last?(uint64_t)((nanos-last)/1000000):0;
    if(gap>=1500&&(lastStallEvent_==std::chrono::steady_clock::time_point{}||
       now-lastStallEvent_>=std::chrono::seconds(5))){
      lastStallEvent_=now;++stallCount_;
      auto maxGap=maxFrameGapMs_.load();
      while(gap>maxGap&&!maxFrameGapMs_.compare_exchange_weak(maxGap,gap)){}
      publishMetrics();
      ps4diag::event(ptvtelemetry::Code::PlaybackStall,0,0,gap);
      ps4diag::note("VIDEO_THREAD","STALL ms="+std::to_string(gap)+
        " empty_polls="+std::to_string(videoEmptyPolls_.load()));
    }
  }
  const uint64_t decoded=frames_.load(),bucket=decoded/120;
  if(decoded>0&&bucket>lastProgressFrameBucket_){
    lastProgressFrameBucket_=bucket;publishMetrics();
    ps4diag::event(ptvtelemetry::Code::PlaybackProgress,0,0,decoded);
    ps4diag::note("PLAYBACK","VIDEO_PROGRESS decoded="+std::to_string(decoded)+
      " presented="+std::to_string(videoPresentedFrames_.load())+
      " skipped="+std::to_string(videoSkippedFrames_.load())+
      " cached="+std::to_string(videoCachedRedraws_.load())+
      " audio_grains="+std::to_string(audioBuffers_.load())+
      " ranges="+std::to_string(remote_.requests()));
  }
  if(!ready)return false;
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
    ps4diag::note("PLAYBACK","PASS first decoded frame displayed");
    publishMetrics();
    ps4diag::event(ptvtelemetry::Code::FirstFrame,0,0,frames_.load());
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
    if(r>=0){status_=Status::Playing;lastDecodedNs_=0;
      ps4diag::event(ptvtelemetry::Code::PlaybackResumed);
      ps4diag::note("PLAYBACK","RESUME user control");}
  }
}
void Player::close(){
  audioRunning_=false;videoRunning_=false;
  // All consumers must stop before the decoder handle and ranged stream.
  if(videoThread_.joinable())videoThread_.join();
  if(audioThread_.joinable())audioThread_.join();
  if(handle_){
    ps4diag::note("PLAYBACK","STOP decoded="+std::to_string(frames_.load())+
      " presented="+std::to_string(videoPresentedFrames_.load()));
    publishMetrics();
    ps4diag::event(ptvtelemetry::Code::PlaybackStopped,0,0,frames_.load());
    sceAvPlayerStop(handle_);sceAvPlayerClose(handle_);handle_=nullptr;
  }
  {
    std::lock_guard<std::mutex> guard(frameMutex_);
    for(auto& f:rgbFrames_)f.clear();
    rgbW_=rgbH_=0;frameVersion_=0;
  }
  remote_.stop();status_=Status::Idle;
}
}
