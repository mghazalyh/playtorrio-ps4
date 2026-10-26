// PlayTorrio TV frontend - OpenOrbis 1280x720 native renderer.
// Font support and display follow the official OpenOrbis font sample.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <sys/stat.h>
#include <cerrno>
#include <thread>
#include <mutex>
#include <atomic>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include "graphics.h"
#include "../src/core.hpp"
#include "diagnostics.hpp"
#include "video_player.hpp"
#include "source_resolver.hpp"

std::stringstream debugLogStream;

namespace {
using C=Color;
const C bg={8,10,15},paper={242,242,251},muted={155,162,184},
  subtle={91,100,125},line={40,44,62},card={23,27,40},
  purple={155,118,255},gold={246,196,109},green={102,206,165};
const char* labels[5]={"Home","Discover","My List","Addons","Settings"};
const char* cards[5]={"Movies","Series","My List","Addons","Settings"};
const char* captions[5]={"Explore films","Browse TV shows","Your saved titles",
                          "Manage sources","Your preferences"};
C lerp(C a,C b,float f){
  auto k=[f](uint8_t x,uint8_t y)->uint8_t {
    return (uint8_t)(x+(y-x)*f);
  };
  return {k(a.r,b.r),k(a.g,b.g),k(a.b,b.b)};
}
void rect(Scene2D& s,int x,int y,int w,int h,C c){
  if(w>0&&h>0&&x>=0&&y>=0&&x+w<=1280&&y+h<=720)
    s.DrawRectangle(x,y,w,h,c);
}
void round(Scene2D& s,int x,int y,int w,int h,int radius,C c){
  const int r=std::min(radius,std::min(w,h)/2);
  if(r==0){rect(s,x,y,w,h,c);return;}
  rect(s,x+r,y,w-2*r,h,c);
  for(int i=0;i<r;i++){
    float d=(float)(r-i-.5f);
    int dx=(int)std::sqrt(float(r*r)-d*d);
    int inset=r-dx;
    rect(s,x+inset,y+i,w-2*inset,1,c);
    rect(s,x+inset,y+h-1-i,w-2*inset,1,c);
  }
}
void circle(Scene2D& s,int cx,int cy,int r,C c){
  for(int y=-r;y<=r;y++){
    int dx=(int)std::sqrt((double)r*r-(double)y*y);
    rect(s,cx-dx,cy+y,dx*2+1,1,c);
  }
}
std::string cut(const std::string& in,size_t n){
  return in.size()<=n?in:in.substr(0,n-3)+"...";
}
struct Fonts {FT_Face xs={},sm={},base={},md={},lg={},xl={};};
void txt(Scene2D& s,const Fonts& f,const std::string& str,int x,int baseline,
         C fg,int size=2){
  FT_Face font=size==0?f.xs:size==1?f.sm:size==2?f.base:
               size==3?f.md:size==4?f.lg:f.xl;
  if(!font)return;
  s.DrawText(const_cast<char*>(str.c_str()),font,x,baseline,bg,fg);
}
void stripe(Scene2D& s){
  for(int x=0;x<1280;x+=12){
    float t=(float)x/1280.f;
    rect(s,x,77,12,223,lerp({21,27,46},{42,27,63},t));
  }
  round(s,902,100,112,193,14,{45,49,82});
  round(s,1018,87,110,206,14,{64,49,94});
  round(s,1132,111,89,180,14,{35,51,76});
  for(int i=0;i<6;i++){
    rect(s,911,114+i*28,93,2,{73,77,119});
    rect(s,1028,105+i*31,90,2,{99,77,136});
    rect(s,1141,123+i*27,70,2,{61,81,117});
  }
  rect(s,0,298,1280,2,{46,39,70});
}
void statusPill(Scene2D& s,const Fonts& font,bool connected,int x,int y,
                const std::string& title){
  round(s,x,y,connected?166:180,34,17,connected?C{28,65,57}:C{46,40,60});
  circle(s,x+17,y+17,5,connected?green:gold);
  txt(s,font,title,x+31,y+23,connected?green:gold,1);
}
void nav(Scene2D& s,const Fonts& fonts,int tab){
  rect(s,0,0,1280,76,{12,15,26});
  round(s,40,24,25,25,8,purple);
  rect(s,49,32,7,7,{250,245,255});
  txt(s,fonts,"PlayTorrio",79,54,paper,3);
  const int pos[]={486,592,712,842,952};
  for(int i=0;i<5;i++){
    txt(s,fonts,labels[i],pos[i],47,tab==i?paper:muted,1);
    if(tab==i)round(s,pos[i],70,42,3,1,purple);
  }
  round(s,1131,24,109,29,14,{35,37,56});
  txt(s,fonts,"PS4 ALPHA",1148,44,{196,175,247},0);
}
void heading(Scene2D& s,const Fonts& f,const std::string& title,
             const std::string& subtitle){
  txt(s,f,title,51,123,paper,4);
  txt(s,f,subtitle,52,155,muted,1);
}
void featureCard(Scene2D& s,const Fonts& f,int x,int y,int index,bool selected){
  constexpr int w=182,h=153;
  const char* names[]={"Movies","TV Series","My List","Addons","TorBox","Settings"};
  const char* small[]={"Browse films","Seasons & episodes","GitHub watchlist",
                      "Stremio sources","Debrid account","Import & playback"};
  const char* label[]={"EXPLORE","EPISODES","SYNC","SOURCES","DEBRID","CONFIG"};
  const C tones[]={{57,38,89},{29,62,84},{38,60,65},
                   {54,44,83},{39,57,77},{71,47,63}};
  C col=tones[index%6];
  round(s,x-2,y-2,w+4,h+4,12,selected?purple:line);
  round(s,x,y,w,h,10,col);
  for(int i=0;i<48;i+=4){
    C glow=lerp(col,{114,88,148},(float)i/104.f);
    rect(s,x+w-49+i,y+10,4,91,glow);
  }
  round(s,x+15,y+16,76,23,8,{25,29,43});
  txt(s,f,label[index%6],x+23,y+33,{198,189,223},0);
  rect(s,x+15,y+78,w-30,1,{101,95,124});
  txt(s,f,names[index%6],x+14,y+113,paper,2);
  txt(s,f,small[index%6],x+15,y+138,{199,194,214},0);
  if(selected)rect(s,x+15,y+h-4,w-30,3,{226,201,255});
}
void miniCard(Scene2D& s,const Fonts& f,int x,int y,int index,
              bool selected,const ps4diag::Summary& diag){
  constexpr int w=182,h=70;
  const char* headings[]={"My List","Import settings","Test services","My addons",
                         "Try playback","Playback logs"};
  const char* desc[]={"Your saved titles","USB / local JSON","Press Triangle",
                     "Manage manifests","Start a stream","FTP diagnostics"};
  std::string hint=desc[index];
  if(index==0&&diag.library_titles>0)
    hint=std::to_string(diag.library_titles)+" titles in cloud";
  if(index==3&&diag.addon_streams>0)
    hint=std::to_string(diag.addon_streams)+" stream sources";
  round(s,x-2,y-2,w+4,h+4,11,selected?purple:line);
  round(s,x,y,w,h,9,{23,29,43});
  rect(s,x+14,y+15,3,43,index%2?C{65,147,179}:purple);
  txt(s,f,headings[index],x+27,y+33,paper,1);
  txt(s,f,hint,x+27,y+59,muted,0);
}
void home(Scene2D& s,const Fonts& f,int cardFocus,
          const ps4diag::Summary& diag){
  stripe(s);
  round(s,51,103,139,23,11,{67,49,91});
  txt(s,f,"PLAYTORRIO FOR TV",62,120,{219,206,255},0);
  txt(s,f,"Your next watch.",49,194,paper,5);
  txt(s,f,"All your sources and your personal library. One screen.",52,233,muted,2);
  txt(s,f,"STREMIO    TORBOX    MY LIST",53,267,{197,177,232},0);
  txt(s,f,"Browse",52,342,paper,3);
  txt(s,f,"Collections and account services",925,339,muted,0);
  for(int i=0;i<6;i++)featureCard(s,f,53+i*198,368,i,i==cardFocus);
  txt(s,f,"Quick access",52,558,paper,3);
  txt(s,f,"Configured for your console",965,556,muted,0);
  for(int i=0;i<6;i++)miniCard(s,f,53+i*198,570,i,i+6==cardFocus,diag);
}
void infoBox(Scene2D& s,const Fonts& f,int y,const std::string& name,
             const std::string& description,const std::string& info,
             bool highlighted=false){
  constexpr int h=66;
  round(s,51,y,1176,h,10,highlighted?C{42,36,70}:card);
  if(highlighted)round(s,51,y,4,h,2,purple);
  txt(s,f,cut(name,47),72,y+27,paper,2);
  txt(s,f,cut(description,95),73,y+51,muted,1);
  if(!info.empty())
    txt(s,f,cut(info,23),1010,y+37,highlighted?green:muted,0);
}
void section(Scene2D& s,const Fonts& f,int tab,const ptv::Settings& cfg,
             bool loaded,int selection,const std::string& error,
             const ps4diag::Summary& diag){
  // Six complete, compact rows on a 720p television (was three).
  const int rows[]={173,255,337,419,501,583};
  if(tab==1){
    heading(s,f,"Discover","Catalog and playback integration status");
    infoBox(s,f,rows[0],"Movies","TMDB movie collections","Coming soon",selection==0);
    infoBox(s,f,rows[1],"Series","Season and episode browsing","Coming soon",selection==1);
    infoBox(s,f,rows[2],"Sources","Stremio stream parsing","Parser ready",selection==2);
    infoBox(s,f,rows[3],"Search","Search across films and shows","Coming soon",selection==3);
    infoBox(s,f,rows[4],"English audio and subtitles","Language-first playback preferences","Coming soon",selection==4);
    infoBox(s,f,rows[5],"Next episode","Continuous series playback","Coming soon",selection==5);
  }else if(tab==2){
    if(!cfg.github_sync_enabled){
      heading(s,f,"My List","GitHub cloud library synchronization is paused");
      infoBox(s,f,rows[0],"GitHub sync","Disabled by your local settings.json","OFF",selection==0);
      infoBox(s,f,rows[1],"Your current library","The PS4 will not contact your GitHub repository","No sync",selection==1);
      infoBox(s,f,rows[2],"Change later","Set githubSyncEnabled to true when ready","Optional",selection==2);
      infoBox(s,f,rows[3],"Movies & TV","Discover from TMDB and configured Stremio addons","Available",selection==3);
      return;
    }
    heading(s,f,"My List","Live read from your private GitHub watchlist");
    if(diag.library_titles>0){
      infoBox(s,f,rows[0],"Private watchlist",
          "Fetched via authenticated GitHub API",
          std::to_string(diag.library_titles)+" titles",selection==0);
      for(int i=0;i<5;i++){
        bool has=i<(int)diag.preview_titles.size();
        const std::string title=has?cut(diag.preview_titles[i],61):"More saved titles...";
        infoBox(s,f,rows[i+1],has?title:"Library",
                has?"Present in your private GitHub library":"Run Triangle to refresh your library",
                has?"Synced":"",selection==i+1);
      }
    }else{
      infoBox(s,f,rows[0],"Repository","GitHub data source",
              cut(cfg.github_repo,23),selection==0);
      infoBox(s,f,rows[1],"Database","playtorrio/watchlist-v1.json",
              "Run Triangle",selection==1);
      infoBox(s,f,rows[2],"Authentication",cfg.github_token.empty()?
              "Key not imported":"GitHub key stored in local settings",
              cfg.github_token.empty()?"Missing":"Imported",selection==2);
      infoBox(s,f,rows[3],"Read/write verification",
              "One test checks download and SHA-guarded upload","Run Triangle",selection==3);
      infoBox(s,f,rows[4],"Offline JSON cache",
              "Stored at /data/playtorrio/library-cache.json","After test",selection==4);
      infoBox(s,f,rows[5],"Playback synchronization","Not implemented in this preview",
              "Unavailable",selection==5);
    }
  }else if(tab==3){
    heading(s,f,"Addons","Your saved Stremio manifest.json sources");
    infoBox(s,f,rows[0],"Installed manifests",
            "Stored locally in settings.json",std::to_string(cfg.addons.size())+" saved",selection==0);
    for(int i=0;i<5;i++){
      bool present=i<(int)cfg.addons.size();
      const std::string title="Addon "+std::to_string(i+1);
      const std::string subtitle=present?cut(cfg.addons[i],91):"No manifest configured";
      infoBox(s,f,rows[i+1],title,subtitle,present?"Imported":"Empty",selection==i+1);
    }
  }else{
    heading(s,f,"Settings","Your account and device preferences");
    infoBox(s,f,rows[0],"Reload local settings","X or Square reloads /data/playtorrio/settings.json",
            loaded?"Loaded":"Not loaded",selection==0);
    infoBox(s,f,rows[1],"TorBox API key","Stored locally and never displayed",
            cfg.torbox_api_key.empty()?"Missing":"Imported",selection==1);
    infoBox(s,f,rows[2],"TMDB API key","Imported from your local settings.json",
            cfg.tmdb_api_key.empty()?"Not set":"Loaded",selection==2);
    infoBox(s,f,rows[3],"Addon manifests","Configured Stremio manifest.json URL list",
            std::to_string(cfg.addons.size())+" configured",selection==3);
    infoBox(s,f,rows[4],"English subtitles","Automatically prefer English when playback works",
            cfg.subtitles_english?"Preferred":"Disabled",selection==4);
    infoBox(s,f,rows[5],"GitHub library sync","Optional cloud library sync (currently off by default)",
            cfg.github_sync_enabled?"Enabled":"Disabled",selection==5);
    if(!error.empty())
      txt(s,f,cut(error,86),72,665,gold,0);
  }
}
std::string readConfig(){
  std::ifstream file("/data/playtorrio/settings.json");
  if(!file.good())return "";
  std::ostringstream out;out<<file.rdbuf();
  auto data=out.str();
  return data.size()>256*1024?"":data;
}
// A real, controller-accessible import path; do not require FTP to set up.
// The imported file is validated before replacing the PS4's local copy.
bool importUsb(ptv::Settings& settings,std::string& message){
  const char* locations[]={
    "/mnt/usb0/playtorrio/settings.json",
    "/mnt/usb0/playtorrio-settings.json",
    "/mnt/usb1/playtorrio/settings.json",
    "/mnt/usb1/playtorrio-settings.json"
  };
  for(const char* source:locations){
    std::ifstream input(source,std::ios::binary);
    if(!input.good())continue;
    std::ostringstream buffer;buffer<<input.rdbuf();
    auto json=buffer.str();
    if(json.empty()||json.size()>256*1024){
      message="Invalid settings size. Maximum 256 KB";
      return false;
    }
    ptv::Settings parsed;
    if(!ptv::import_settings(json,parsed,message))return false;
    if(::mkdir("/data/playtorrio",0700)<0 && errno!=EEXIST){
      message="Cannot create /data/playtorrio folder";
      return false;
    }
    const char* temp="/data/playtorrio/settings.json.tmp";
    std::ofstream output(temp,std::ios::binary|std::ios::trunc);
    if(!output.good()){
      message="Cannot write local settings storage";
      return false;
    }
    output.write(json.data(),json.size());output.close();
    if(!output.good()||::rename(temp,"/data/playtorrio/settings.json")!=0){
      ::remove(temp);
      message="Failed saving imported settings";
      return false;
    }
    ::chmod("/data/playtorrio/settings.json",0600);
    settings=parsed;
    message="USB settings imported successfully";
    return true;
  }
  // shadPS4 users edit the mapped Windows data folder directly. Pressing
  // Square reloads the same /data file, with no USB or app restart needed.
  auto local=readConfig();
  if(!local.empty()){
    ptv::Settings parsed;std::string error;
    if(ptv::import_settings(local,parsed,error)){
      settings=parsed;
      message="Local settings reloaded";
      return true;
    }
    message="Invalid local settings JSON (check syntax)";
    return false;
  }
  message="No local JSON; place settings.json in /data/playtorrio";
  return false;
}
void diagnosticBand(Scene2D& s,const Fonts& f,const ps4diag::Summary& report,
                    bool ran,bool logging,bool testing) {
  round(s,51,648,1176,29,9,{29,31,44});
  const std::string label=testing?"TESTING NETWORK SERVICES, PLEASE WATCH FTP LOG..."
    :ran?
    ("NETWORK TEST: "+std::to_string(report.passed)+" PASS  "+
     std::to_string(report.failed)+" FAIL  "+
     std::to_string(report.skipped)+" SKIP   STREAMS "+
     std::to_string(report.addon_streams)+"   LIBRARY "+
     std::to_string(report.library_titles)):
    "Press TRIANGLE to test addons, TorBox, stream transfer, and GitHub";
  txt(s,f,cut(label,107),62,668,report.failed?gold:green,0);
  if(!logging)txt(s,f,"LOG NOT WRITABLE",992,668,gold,0);
}
void footer(Scene2D& s,const Fonts& f,int tab,int focus){
  rect(s,0,684,1280,36,{16,19,28});
  circle(s,60,702,8,{61,158,117});
  txt(s,f,"X",55,706,paper,0);
  txt(s,f,tab==0?"Open section":"Select",79,707,muted,0);
  circle(s,231,702,8,{161,102,109});
  txt(s,f,"O",227,706,paper,0);
  txt(s,f,"Back",251,707,muted,0);
  txt(s,f,"L1 / R1   Tabs",387,707,muted,0);
  txt(s,f,"D-Pad   Navigate",594,707,muted,0);
  txt(s,f,"Triangle   Test all",798,707,muted,0);
  if(tab==4)txt(s,f,"Square Reload",1105,707,muted,0);
}

}

int main(){
  const bool logging=ps4diag::start_logging();
  ps4diag::note("BOOT","Entering native PS4 startup checks");
  ptv::Settings settings;std::string parseError;
  auto source=readConfig();
  bool imported=!source.empty()&&ptv::import_settings(source,settings,parseError);
  if(source.empty())parseError="Place settings.json in /data/playtorrio";
  scePadInit();
  OrbisUserServiceInitializeParams params={};
  params.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;
  sceUserServiceInitialize(&params);
  int user=-1;sceUserServiceGetInitialUser(&user);
  int pad=scePadOpen(user,0,0,nullptr);
  Scene2D* screen=new Scene2D(1280,720,4);
  if(!screen->Init(0xC000000,2)){
    ps4diag::note("BOOT","FAIL OpenOrbis Scene2D initialized with error");
    return 1;
  }
  ps4diag::note("BOOT","PASS Scene2D initialized");

  const char* fontPath="/app0/assets/fonts/Gontserrat-Regular.ttf";
  Fonts fonts;
  if(!screen->InitFont(&fonts.xs,fontPath,15)||
     !screen->InitFont(&fonts.sm,fontPath,18)||
     !screen->InitFont(&fonts.base,fontPath,23)||
     !screen->InitFont(&fonts.md,fontPath,28)||
     !screen->InitFont(&fonts.lg,fontPath,35)||
     !screen->InitFont(&fonts.xl,fontPath,52)){
    ps4diag::note("BOOT","FAIL OpenOrbis FreeType font initialization");
    return 2;
  }
  ps4diag::note("BOOT","PASS Gontserrat UI fonts loaded");

  ps4diag::note("BOOT","Native dashboard initialized; press TRIANGLE to test network");
  int tab=0,focus=0,frame=0;
  ps4diag::Summary diagnostic;
  std::mutex diagnosticMutex;
  std::atomic<bool> running{false};
  bool tested=false;
  ps4video::Player player;
  bool viewing=false;
  bool overlay=true;
  std::atomic<bool> sourceBusy{false};
  std::mutex sourceMutex;
  std::string preparedUrl, preparedError;
  bool sourceReady=false;
  auto requestPlayback=[&](std::string mediaType,std::string mediaId){
    if(viewing||sourceBusy.exchange(true))return;
    preparedError.clear();
    const auto config=settings;
    ps4diag::note("CONTROL","Requesting playable video from local sources");
    std::thread([&,config,mediaType,mediaId](){
      auto found=ps4source::resolve(config,mediaType,mediaId);
      {
        std::lock_guard<std::mutex> guard(sourceMutex);
        preparedUrl=std::move(found.url);
        preparedError=found.reason;
        sourceReady=true;
      }
      sourceBusy.store(false);
    }).detach();
  };
  uint32_t previous=0;
  while(true){
    OrbisPadData data={};
    if(pad>=0&&scePadReadState(pad,&data)>=0){
      uint32_t pressed=data.buttons&~previous;
      previous=data.buttons;
      if(viewing){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){
          player.close();viewing=false;overlay=true;
          ps4diag::note("PLAYBACK","STOP user exited video");
        }else if(pressed&(ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_SQUARE)){
          player.pauseToggle();overlay=true;
        }else if(pressed&ORBIS_PAD_BUTTON_TRIANGLE){
          overlay=!overlay;
        }
        pressed=0;
      }
      if((pressed&ORBIS_PAD_BUTTON_TRIANGLE) && !running.exchange(true)){
        // Keep the TV interface responsive while live HTTPS calls execute.
        // The worker owns a snapshot of settings and publishes the result
        // under mutex; it never touches the graphics API.
        const auto snapshot=settings;
        std::thread([&,snapshot](){
          ps4diag::note("CONTROL","User requested full diagnostics");
          auto result=ps4diag::run(snapshot,true);
          {
            std::lock_guard<std::mutex> guard(diagnosticMutex);
            diagnostic=std::move(result);
            tested=true;
          }
          running.store(false);
        }).detach();
      }
      if(pressed&ORBIS_PAD_BUTTON_R1){tab=(tab+1)%5;focus=0;}
      if(pressed&ORBIS_PAD_BUTTON_L1){tab=(tab+4)%5;focus=0;}
      const int tabBeforeSelection=tab;
      if(tab==0){
        if(pressed&ORBIS_PAD_BUTTON_RIGHT)focus=(focus/6)*6+(focus%6+1)%6;
        if(pressed&ORBIS_PAD_BUTTON_LEFT)focus=(focus/6)*6+(focus%6+5)%6;
        if(pressed&ORBIS_PAD_BUTTON_DOWN)focus=(focus+6)%12;
        if(pressed&ORBIS_PAD_BUTTON_UP)focus=(focus+6)%12;
        if(pressed&ORBIS_PAD_BUTTON_CROSS){
          if(focus==8){
            if(!running.exchange(true)){
              const auto snapshot=settings;
              std::thread([&,snapshot](){
                ps4diag::note("CONTROL","Starting service checks from quick access");
                auto result=ps4diag::run(snapshot,true);
                {std::lock_guard<std::mutex> guard(diagnosticMutex);
                 diagnostic=std::move(result);tested=true;}
                running.store(false);
              }).detach();
            }
          }else if(focus==10){
            requestPlayback(settings.diagnostic_type,settings.diagnostic_id);
          }else{
            const int destinations[]={1,1,2,3,4,4,2,4,0,3,0,4};
            const int option=focus;
            tab=destinations[option];
            focus=0;
          }
        }
      }else{
        if(pressed&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_DOWN))
          focus=(focus+1)%6;
        if(pressed&(ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_UP))
          focus=(focus+5)%6;
      }
      if(pressed&ORBIS_PAD_BUTTON_CIRCLE){tab=0;focus=0;}
      if(!running.load() && ((pressed&ORBIS_PAD_BUTTON_SQUARE && tab==4)||
         (pressed&ORBIS_PAD_BUTTON_CROSS && tab==4 && focus==0 && tabBeforeSelection==4))){
        std::string result;
        imported=importUsb(settings,result);
        parseError=result;
        ps4diag::note("IMPORT",imported?
          "PASS local USB settings installed":"FAIL missing/invalid USB settings");
      }
      if(pressed&ORBIS_PAD_BUTTON_CROSS && tab==1 && tabBeforeSelection==1){
        requestPlayback(focus==1?"series":"movie",settings.diagnostic_id);
      }
      if(pressed&ORBIS_PAD_BUTTON_CROSS && tab==2 && tabBeforeSelection==2){
        std::string type="movie",id;
        {
          std::lock_guard<std::mutex> guard(diagnosticMutex);
          if(focus>=1 && focus-1<(int)diagnostic.preview_ids.size()){
            id=diagnostic.preview_ids[focus-1];
            type=diagnostic.preview_types[focus-1];
          }
        }
        requestPlayback(type,id);
      }
      if(pressed&ORBIS_PAD_BUTTON_CROSS && tab==3 && tabBeforeSelection==3){
        requestPlayback(settings.diagnostic_type,settings.diagnostic_id);
      }
    }
    ps4diag::Summary snapshot;
    bool completed=false;
    {
      std::lock_guard<std::mutex> guard(diagnosticMutex);
      snapshot=diagnostic;
      completed=tested;
    }
    // Transfer a successfully resolved URL on the render thread, where the
    // decoder's lifetime is managed. The secret URL is never shown/logged.
    std::string openingUrl,openingStatus;
    bool hasReady=false;
    {
      std::lock_guard<std::mutex> lock(sourceMutex);
      if(sourceReady){
        openingUrl.swap(preparedUrl);
        openingStatus.swap(preparedError);
        sourceReady=false;hasReady=true;
      }
    }
    if(hasReady){
      if(openingUrl.empty()){
        parseError=openingStatus;
        ps4diag::note("PLAYBACK","FAIL resolution did not yield a supported stream");
      }else{
        std::string failure;
        if(player.start(openingUrl,failure)){
          viewing=true;overlay=true;
          ps4diag::note("PLAYBACK","START AvPlayer video session");
        }else parseError=failure;
      }
    }
    if(viewing){
      screen->FrameBufferFill(bg);
      bool frameDrawn=player.render(*screen);
      if(overlay || player.state()!=ps4video::Player::Status::Playing){
        round(*screen,25,24,650,78,12,{24,28,39});
        txt(*screen,fonts,"PlayTorrio - PS4 hardware video",48,56,paper,3);
        txt(*screen,fonts,"X/Square: Pause   Circle: Back   Triangle: Hide UI",48,87,muted,1);
        if(player.state()==ps4video::Player::Status::Opening)
          txt(*screen,fonts,"Buffering stream...",65,135,gold,2);
        if(player.state()==ps4video::Player::Status::Ended)
          txt(*screen,fonts,"Stream ended. Circle to exit.",65,135,gold,2);
        if(player.state()==ps4video::Player::Status::Failed)
          txt(*screen,fonts,"Hardware decoder error. Circle to exit.",65,135,gold,2);
      }
      (void)frameDrawn;
    }else{
      if(tab==0)home(*screen,fonts,focus,snapshot);
      else{
        screen->FrameBufferFill(bg);
        section(*screen,fonts,tab,settings,imported,focus,parseError,snapshot);
      }
      nav(*screen,fonts,tab);
      diagnosticBand(*screen,fonts,snapshot,completed,logging,running.load());
      footer(*screen,fonts,tab,focus);
      if(sourceBusy.load()){
        round(*screen,240,275,800,100,12,{32,30,47});
        txt(*screen,fonts,"Searching Torrentio / TorBox for a playable stream...",269,335,paper,2);
      }else if(!parseError.empty()){
        txt(*screen,fonts,cut(parseError,78),65,641,gold,0);
      }
    }
    screen->SubmitFlip(frame);
    screen->FrameWait(frame);
    screen->FrameBufferSwap();
    ++frame;
    sceKernelUsleep(42000);
  }
}
