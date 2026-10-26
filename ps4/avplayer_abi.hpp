#pragma once
// PS4 libSceAvPlayer ABI subset. OpenOrbis AvPlayer.h uses placeholder
// void declarations; this translation unit must NOT include that header.
// API layout cross-checked against open homebrew AvPlayer integrations.
#include <cstddef>
#include <cstdint>
extern "C" {
struct SceAvPlayerImpl;
using AvHandle=SceAvPlayerImpl*;
using AllocFn=void*(*)(void*,uint32_t,uint32_t);
using FreeFn=void(*)(void*,void*);
using OpenFileFn=int(*)(void*,const char*);
using CloseFileFn=int(*)(void*);
using ReadFileFn=int(*)(void*,uint8_t*,uint64_t,uint32_t);
using FileSizeFn=uint64_t(*)(void*);
using EventFn=void(*)(void*,int32_t,int32_t,void*);
struct AvAllocCallbacks {
  void* object;
  AllocFn allocate;
  FreeFn deallocate;
  AllocFn allocateTexture;
  FreeFn deallocateTexture;
};
struct AvFileCallbacks {
  void* object;
  OpenFileFn open;
  CloseFileFn close;
  ReadFileFn readOffset;
  FileSizeFn size;
};
struct AvEventCallbacks {void* object; EventFn event;};
struct AvInitData {
  AvAllocCallbacks alloc;
  AvFileCallbacks file;
  AvEventCallbacks events;
  uint32_t debugLevel;
  uint32_t basePriority;
  int32_t videoBuffers;
  int32_t autoStart;
  uint8_t reserved[3];
  const char* defaultLanguage;
};
struct AvVideoDetails {
  uint32_t width;
  uint32_t height;
  float aspectRatio;
  uint8_t language[4];
  uint32_t framerate,cropLeft,cropRight,cropTop,cropBottom,pitch;
  uint8_t lumaBits,chromaBits,fullRange;
  uint8_t reserved[37];
};
struct AvAudioDetails {
  uint16_t channels;
  uint8_t reserved[2];
  uint32_t sampleRate,size;
  uint8_t language[4];
  uint8_t reserved1[64];
};
union AvDetails {
  AvVideoDetails video;
  AvAudioDetails audio;
  uint8_t reserved[80];
};
struct AvFrame {
  void* data;
  uint8_t reserved[4];
  uint64_t timestamp;
  AvDetails details;
};
struct AvStreamInfo {
  uint32_t type; // 0 video, 1 audio, 2 timed text
  uint8_t reserved[4];
  AvDetails details;
  uint64_t duration,startTime;
};
AvHandle sceAvPlayerInit(AvInitData*);
int32_t sceAvPlayerAddSource(AvHandle,const char*);
int32_t sceAvPlayerAddSourceEx(AvHandle,int32_t,const char*);
int32_t sceAvPlayerClose(AvHandle);
int32_t sceAvPlayerStop(AvHandle);
int32_t sceAvPlayerPause(AvHandle);
int32_t sceAvPlayerResume(AvHandle);
int32_t sceAvPlayerIsActive(AvHandle);
int32_t sceAvPlayerGetVideoDataEx(AvHandle,AvFrame*);
int32_t sceAvPlayerGetStreamInfo(AvHandle,uint32_t,AvStreamInfo*);
int32_t sceAvPlayerStreamCount(AvHandle);
int32_t sceAvPlayerEnableStream(AvHandle,uint32_t);
int32_t sceAvPlayerDisableStream(AvHandle,uint32_t);
int32_t sceAvPlayerGetAudioData(AvHandle,AvFrame*);
}
static_assert(sizeof(AvAllocCallbacks)==40,"AvPlayer allocator ABI");
static_assert(sizeof(AvFrame)>=100,"AvPlayer frame ABI");
