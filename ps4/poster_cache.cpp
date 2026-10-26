#include "poster_cache.hpp"
#include "native_net.hpp"
#include "graphics.h"
#include <algorithm>
#include <thread>
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#include "../vendor/stb_image.h"

namespace ps4art {
namespace {
constexpr int kW=160,kH=240;
bool valid_path(const std::string& s){
  if(s.size()<5||s.size()>140||s[0]!='/'||s.find("..")!=std::string::npos)return false;
  if(s.rfind(".jpg")!=s.size()-4&&s.rfind(".png")!=s.size()-4&&
     s.rfind(".jpeg")!=s.size()-5)return false;
  for(unsigned char c:s)if(!(std::isalnum(c)||c=='/'||c=='.'||c=='_'||c=='-'))return false;
  return true;
}
std::shared_ptr<Image> fetch_image(const std::string& path){
  auto res=ps4net::request("https://image.tmdb.org/t/p/w342"+path,
                           "GET",{},"",512*1024);
  if(res.status!=200||res.platform_error||res.truncated||res.body.empty())return {};
  int w=0,h=0,channels=0;
  const auto* data=(const stbi_uc*)res.body.data();
  const int size=(int)res.body.size();
  if(!stbi_info_from_memory(data,size,&w,&h,&channels)||
     w<2||h<2||w>2048||h>2048||(int64_t)w*h>1800000)return {};
  stbi_uc* rgb=stbi_load_from_memory(data,size,&w,&h,&channels,3);
  if(!rgb)return {};
  auto picture=std::make_shared<Image>();
  picture->w=kW;picture->h=kH;picture->pixels.resize(kW*kH);
  // Posters arrive pre-sized from TMDB; bilinear interpolation keeps edges.
  for(int y=0;y<kH;y++){
    const int srcY=y*(h-1)/std::max(1,kH-1);
    for(int x=0;x<kW;x++){
      const int srcX=x*(w-1)/std::max(1,kW-1);
      const size_t idx=((size_t)srcY*w+srcX)*3;
      picture->pixels[(size_t)y*kW+x]=0x80000000u|
        ((uint32_t)rgb[idx]<<16)|((uint32_t)rgb[idx+1]<<8)|(uint32_t)rgb[idx+2];
    }
  }
  stbi_image_free(rgb);
  return picture;
}
}
void Cache::request(const std::string& path){
  if(!valid_path(path))return;
  std::lock_guard<std::mutex> guard(lock_);
  auto it=entries_.find(path);
  if(it!=entries_.end()){it->second.seen=++tick_;return;}
  if(workers_>=2)return;
  if(entries_.size()>=28){
    auto oldest=entries_.end();
    for(auto p=entries_.begin();p!=entries_.end();++p)
      if(!p->second.loading&&(oldest==entries_.end()||p->second.seen<oldest->second.seen))oldest=p;
    if(oldest!=entries_.end())entries_.erase(oldest);
  }
  Entry e;e.loading=true;e.seen=++tick_;
  entries_[path]=std::move(e);++workers_;
  std::thread([this,path](){
    auto result=fetch_image(path);
    std::lock_guard<std::mutex> done(lock_);
    const auto it=entries_.find(path);
    if(it!=entries_.end()){
      it->second.image=std::move(result);
      it->second.failed=!it->second.image;
      it->second.loading=false;
    }
    --workers_;
  }).detach();
}
bool Cache::draw(Scene2D& scene,const std::string& path,int x,int y,int w,int h){
  if(x<0||y<0||w<1||h<1||x+w>1280||y+h>720)return false;
  request(path);
  std::shared_ptr<Image> pic;
  {std::lock_guard<std::mutex> guard(lock_);
   const auto it=entries_.find(path);
   if(it!=entries_.end())pic=it->second.image;}
  if(!pic)return false;
  scratch_.resize((size_t)w*h);
  for(int row=0;row<h;row++){
    const int sy=(int)((int64_t)row*pic->h/h);
    for(int col=0;col<w;col++){
      const int sx=(int)((int64_t)col*pic->w/w);
      scratch_[(size_t)row*w+col]=pic->pixels[(size_t)sy*pic->w+sx];
    }
  }
  scene.BlitRgb32(scratch_.data(),x,y,w,h);
  return true;
}
}
