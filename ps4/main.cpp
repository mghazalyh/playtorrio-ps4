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
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include "graphics.h"
#include "../src/core.hpp"
#include "diagnostics.hpp"

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
  // 720p safe-area hero: only 168px tall, leaving room for 16 tiles.
  for(int x=0;x<1280;x+=8){
    float t=(float)x/1280.f;
    rect(s,x,76,8,174,lerp({18,20,34},{38,26,66},t));
  }
  circle(s,1053,163,88,{55,38,89});
  circle(s,1053,163,65,{75,51,116});
  circle(s,1053,163,39,{109,70,151});
}
void statusPill(Scene2D& s,const Fonts& font,bool connected,int x,int y,
                const std::string& title){
  round(s,x,y,connected?166:180,34,17,connected?C{28,65,57}:C{46,40,60});
  circle(s,x+17,y+17,5,connected?green:gold);
  txt(s,font,title,x+31,y+23,connected?green:gold,1);
}
void nav(Scene2D& s,const Fonts& fonts,int tab){
  rect(s,0,0,1280,74,{13,16,26});
  txt(s,fonts,"PlayTorrio",48,49,paper,3);
  circle(s,31,38,7,purple);
  const int xpos[]={500,584,696,798,896};
  for(int i=0;i<5;i++){
    txt(s,fonts,labels[i],xpos[i],45,tab==i?paper:muted,1);
    if(tab==i)round(s,xpos[i],63,34,2,1,purple);
  }
  round(s,1133,20,101,30,13,{37,32,52});
  txt(s,fonts,"PS4 PREVIEW",1146,41,{201,174,255},0);
}
void heading(Scene2D& s,const Fonts& f,const std::string& title,
             const std::string& subtitle){
  txt(s,f,title,51,123,paper,4);
  txt(s,f,subtitle,52,155,muted,1);
}
void featureCard(Scene2D& s,const Fonts& f,int x,int y,int index,bool selected){
  constexpr int w=132;
  constexpr int h=137;
  const char* names[]={"Movies","Series","My List","Addons","TorBox","Settings","Search","History",
                       "Library","Manifests","Keys","USB import","Streams","Audio","Subtitles","Player"};
  const char* tags[]={"BROWSE","EPISODES","CLOUD","STREMIO","DEBRID","CONFIG","FIND","ACTIVITY",
                      "JSON V1","SOURCES","PRIVATE","SETUP","PARSERS","ENGLISH","ENGLISH","PREVIEW"};
  const C hues[]={{51,35,82},{30,66,84},{72,42,75},{46,47,84},
                  {27,76,70},{66,45,69},{49,52,91},{49,56,70}};
  C hue=hues[index%8];
  round(s,x,y,w,h,10,selected?purple:line);
  round(s,x+2,y+2,w-4,h-4,9,lerp(hue,bg,.25f));
  round(s,x+11,y+11,67,19,8,{33,29,48});
  txt(s,f,tags[index],x+16,y+26,{207,192,239},0);
  circle(s,x+95,y+51,23,lerp(hue,purple,.26f));
  circle(s,x+95,y+51,12,lerp(hue,paper,.16f));
  txt(s,f,names[index],x+11,y+103,paper,1);
  if(index>=8)
    txt(s,f,index==8?"Same format":index==9?"Saved URLs":index==10?"On console":
            index==11?"From USB":index==12?"No network":index==13?"Planned":
            index==14?"Planned":"Not ready",x+11,y+122,muted,0);
  else txt(s,f,"Open section",x+11,y+122,muted,0);
  if(selected)round(s,x+11,y+h-4,w-22,2,1,{236,206,255});
}
void home(Scene2D& s,const Fonts& f,int cardFocus){
  stripe(s);
  round(s,54,93,128,22,10,{64,49,99});
  txt(s,f,"EARLY PREVIEW",66,109,{228,213,255},0);
  txt(s,f,"PlayTorrio for PS4",52,166,paper,5);
  txt(s,f,"Browse your sections with the controller.",54,206,muted,1);
  round(s,903,191,243,27,12,{55,40,83});
  txt(s,f,"STREAMING STILL IN DEVELOPMENT",915,210,{229,210,255},0);
  txt(s,f,"EXPLORE",52,285,paper,2);
  txt(s,f,"8 sections per row",1034,284,muted,0);
  for(int i=0;i<8;i++)featureCard(s,f,52+i*148,301,i,i==cardFocus);
  txt(s,f,"YOUR PLAYTORRIO",52,479,paper,2);
  txt(s,f,"Local setup and feature status",917,478,muted,0);
  for(int i=0;i<8;i++)featureCard(s,f,52+i*148,494,i+8,i+8==cardFocus);
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
    infoBox(s,f,rows[0],"Import settings","X or Square loads JSON from USB0 or USB1",
            loaded?"Imported":"Not loaded",selection==0);
    infoBox(s,f,rows[1],"TorBox API key","Stored locally and never displayed",
            cfg.torbox_api_key.empty()?"Missing":"Imported",selection==1);
    infoBox(s,f,rows[2],"Private GitHub key","Your torriodb access token remains on PS4",
            cfg.github_token.empty()?"Missing":"Imported",selection==2);
    infoBox(s,f,rows[3],"Addon manifests","Configured Stremio URL list",
            std::to_string(cfg.addons.size())+" configured",selection==3);
    infoBox(s,f,rows[4],"English subtitles","Automatically prefer English when playback works",
            cfg.subtitles_english?"Preferred":"Disabled",selection==4);
    infoBox(s,f,rows[5],"English audio","Automatically prefer labeled English tracks",
            cfg.audio_english?"Preferred":"Disabled",selection==5);
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
  message="No settings JSON found on USB0 or USB1";
  return false;
}
void diagnosticBand(Scene2D& s,const Fonts& f,const ps4diag::Summary& report,
                    bool ran,bool logging) {
  round(s,51,648,1176,29,9,{29,31,44});
  const std::string label=ran?
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
  if(tab==4)txt(s,f,"Square USB",1120,707,muted,0);
}

}

int main(){
  ptv::Settings settings;std::string parseError;
  auto source=readConfig();
  bool imported=!source.empty()&&ptv::import_settings(source,settings,parseError);
  if(source.empty())parseError="Add settings.json using GoldHEN FTP";
  scePadInit();
  OrbisUserServiceInitializeParams params={};
  params.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;
  sceUserServiceInitialize(&params);
  int user=-1;sceUserServiceGetInitialUser(&user);
  int pad=scePadOpen(user,0,0,nullptr);
  Scene2D* screen=new Scene2D(1280,720,4);
  if(!screen->Init(0xC000000,2))return 1;

  const char* fontPath="/app0/assets/fonts/Gontserrat-Regular.ttf";
  Fonts fonts;
  if(!screen->InitFont(&fonts.xs,fontPath,15)||
     !screen->InitFont(&fonts.sm,fontPath,18)||
     !screen->InitFont(&fonts.base,fontPath,23)||
     !screen->InitFont(&fonts.md,fontPath,28)||
     !screen->InitFont(&fonts.lg,fontPath,35)||
     !screen->InitFont(&fonts.xl,fontPath,52))return 2;

  const bool logging=ps4diag::start_logging();
  ps4diag::note("BOOT","Native application launched; use TRIANGLE to run all network checks");
  int tab=0,focus=0,frame=0;
  ps4diag::Summary diagnostic;
  bool tested=false;
  uint32_t previous=0;
  while(true){
    OrbisPadData data={};
    if(pad>=0&&scePadReadState(pad,&data)>=0){
      uint32_t pressed=data.buttons&~previous;
      previous=data.buttons;
      if(pressed&ORBIS_PAD_BUTTON_TRIANGLE){
        // Synchronous suite; persistent per-request flushing makes it useful
        // even when a firmware-specific network API hangs or the app crashes.
        ps4diag::note("CONTROL","User requested full diagnostics");
        diagnostic=ps4diag::run(settings,true);
        tested=true;
      }
      if(pressed&ORBIS_PAD_BUTTON_R1){tab=(tab+1)%5;focus=0;}
      if(pressed&ORBIS_PAD_BUTTON_L1){tab=(tab+4)%5;focus=0;}
      if(tab==0){
        if(pressed&ORBIS_PAD_BUTTON_RIGHT)focus=(focus/8)*8+(focus%8+1)%8;
        if(pressed&ORBIS_PAD_BUTTON_LEFT)focus=(focus/8)*8+(focus%8+7)%8;
        if(pressed&ORBIS_PAD_BUTTON_DOWN)focus=(focus+8)%16;
        if(pressed&ORBIS_PAD_BUTTON_UP)focus=(focus+8)%16;
        if(pressed&ORBIS_PAD_BUTTON_CROSS){
          const int destinations[]={1,1,2,3,4,4,1,2,2,3,4,4,1,1,4,1};
          const int option=focus;
          tab=destinations[option];
          focus=(option==4||option==10)?1:0;
        }
      }else{
        if(pressed&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_DOWN))
          focus=(focus+1)%6;
        if(pressed&(ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_UP))
          focus=(focus+5)%6;
      }
      if(pressed&ORBIS_PAD_BUTTON_CIRCLE){tab=0;focus=0;}
      if((pressed&ORBIS_PAD_BUTTON_SQUARE && tab==4)||
         (pressed&ORBIS_PAD_BUTTON_CROSS && tab==4 && focus==0)){
        std::string result;
        imported=importUsb(settings,result);
        parseError=result;
        ps4diag::note("IMPORT",imported?
          "PASS local USB settings installed":"FAIL missing/invalid USB settings");
      }
      if(pressed&ORBIS_PAD_BUTTON_CROSS && tab==1){
        parseError="Catalog connection is not included in this preview";
      }
      if(pressed&ORBIS_PAD_BUTTON_CROSS && tab==2){
        parseError="GitHub live sync is not yet implemented on PS4";
      }
      if(pressed&ORBIS_PAD_BUTTON_CROSS && tab==3){
        parseError="Addon fetching is not yet implemented on PS4";
      }
    }
    if(tab==0)home(*screen,fonts,focus);
    else{
      screen->FrameBufferFill(bg);
      section(*screen,fonts,tab,settings,imported,focus,parseError,diagnostic);
    }
    nav(*screen,fonts,tab);
    diagnosticBand(*screen,fonts,diagnostic,tested,logging);
    footer(*screen,fonts,tab,focus);
    screen->SubmitFlip(frame);
    screen->FrameWait(frame);
    screen->FrameBufferSwap();
    ++frame;
    sceKernelUsleep(42000);
  }
}
