// PlayTorrio TV frontend - OpenOrbis 1280x720 native renderer.
// Font support and display follow the official OpenOrbis font sample.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include "graphics.h"
#include "../src/core.hpp"

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
  for(int x=0;x<1280;x+=8){
    float t=(float)x/1280.f;
    C c=lerp({16,17,28},{35,21,62},t);
    rect(s,x,91,8,306,c);
  }
  // Decorative lights stay behind content. Keep contrast on the text half.
  circle(s,1040,250,190,{38,28,73});
  circle(s,1040,250,149,{58,38,98});
  circle(s,1040,250,103,{90,54,138});
  circle(s,1040,250,60,{120,72,186});
  circle(s,1040,250,26,{165,103,225});
  round(s,885,370,300,8,4,{91,53,153});
}
void statusPill(Scene2D& s,const Fonts& font,bool connected,int x,int y,
                const std::string& title){
  round(s,x,y,connected?166:180,34,17,connected?C{28,65,57}:C{46,40,60});
  circle(s,x+17,y+17,5,connected?green:gold);
  txt(s,font,title,x+31,y+23,connected?green:gold,1);
}
void nav(Scene2D& s,const Fonts& fonts,int tab){
  txt(s,fonts,"PlayTorrio",55,59,paper,4);
  circle(s,35,45,10,purple);
  rect(s,31,41,8,8,{255,255,255});
  const int xpos[]={455,553,679,789,910};
  for(int i=0;i<5;i++){
    txt(s,fonts,labels[i],xpos[i],56,tab==i?paper:muted,2);
    if(tab==i)round(s,xpos[i],75,45,3,2,purple);
  }
  round(s,1128,24,112,35,17,{43,37,59});
  txt(s,fonts,"PS4 BETA",1144,47,{201,174,255},1);
}
void heading(Scene2D& s,const Fonts& f,const std::string& title,
             const std::string& subtitle){
  txt(s,f,title,58,171,paper,5);
  txt(s,f,subtitle,59,211,muted,2);
}
void featureCard(Scene2D& s,const Fonts& f,int x,int y,int index,bool selected){
  const int w=218,h=164;
  const C colors[]={ {52,34,90},{30,73,94},{75,45,68},{53,46,94},{48,61,78} };
  round(s,x,y,w,h,15,selected?purple:line);
  round(s,x+3,y+3,w-6,h-6,13,colors[index]);
  circle(s,x+161,y+47,60,lerp(colors[index],purple,.27f));
  circle(s,x+186,y+26,27,lerp(colors[index],paper,.16f));
  round(s,x+20,y+20,68,24,12,{27,25,40});
  txt(s,f,index==0?"FILMS":index==1?"SHOWS":index==2?"CLOUD":
       index==3?"TOOLS":"SYSTEM",x+29,y+37,{217,204,254},0);
  txt(s,f,cards[index],x+20,y+107,paper,3);
  txt(s,f,captions[index],x+20,y+134,{204,204,221},1);
  if(selected)round(s,x+20,y+148,w-40,3,1,{230,208,255});
}
void home(Scene2D& s,const Fonts& f,int cardFocus){
  stripe(s);
  round(s,58,132,185,32,16,{63,48,92});
  circle(s,77,148,5,gold);
  txt(s,f,"NATIVE PS4 PREVIEW",90,155,{228,213,255},0);
  txt(s,f,"Your stories.",57,239,paper,5);
  txt(s,f,"On the big screen.",57,303,paper,5);
  txt(s,f,"A new PlayTorrio experience is taking shape.",59,348,muted,2);
  round(s,58,370,209,40,12,purple);
  txt(s,f,"Browse categories",78,397,{13,11,24},2);
  txt(s,f,"DISCOVER",58,462,paper,3);
  txt(s,f,"Early preview | Feature work continues",785,456,muted,1);
  for(int i=0;i<5;i++)featureCard(s,f,58+i*243,483,i,i==cardFocus);
}
void infoBox(Scene2D& s,const Fonts& f,int y,const std::string& name,
             const std::string& description,const std::string& info,
             bool highlighted=false){
  round(s,57,y,1167,86,15,highlighted?C{40,36,65}:card);
  if(highlighted)round(s,57,y,5,86,2,purple);
  txt(s,f,name,83,y+35,paper,3);
  txt(s,f,description,84,y+65,muted,1);
  if(!info.empty())txt(s,f,cut(info,28),925,y+49,highlighted?green:muted,1);
}
void section(Scene2D& s,const Fonts& f,int tab,const ptv::Settings& cfg,
             bool loaded,int selection,const std::string& error){
  if(tab==1){
    heading(s,f,"Discover","Find movies and series from your installed catalog providers.");
    infoBox(s,f,252,"Movies","TMDB-powered collections and featured titles","Integration pending",selection==0);
    infoBox(s,f,352,"Series","Seasons, episodes and next episode actions","Integration pending",selection==1);
    infoBox(s,f,452,"Sources","Stremio manifest and stream parsers available","Network pending",selection==2);
  }else if(tab==2){
    heading(s,f,"My List","One library across your Windows, iPad and PS4.");
    infoBox(s,f,252,"Your private database","Settings refer to the same torriodb JSON schema",
            cut(cfg.github_repo,23),selection==0);
    infoBox(s,f,352,"GitHub access",cfg.github_token.empty()?
            "No key imported on this PS4":"Local key imported; never displayed on screen",
            cfg.github_token.empty()?"Not configured":"Key imported",selection==1);
    infoBox(s,f,452,"Device synchronization",
            "The live GitHub HTTP adapter is not implemented yet",
            "Not connected",selection==2);
  }else if(tab==3){
    heading(s,f,"Addons","Import your Stremio sources by their manifest.json URLs.");
    infoBox(s,f,252,"Installed manifests","Read securely from your local settings file",
            std::to_string(cfg.addons.size())+" configured",selection==0);
    for(int i=0;i<2;i++){
      bool present=i<(int)cfg.addons.size();
      std::string label=present?cut(cfg.addons[i],65):"No addon in this slot";
      infoBox(s,f,352+i*100,"Addon "+std::to_string(i+1),
              label,present?"Imported":"Empty",selection==i+1);
    }
  }else{
    heading(s,f,"Settings","Your configuration stays on your console.");
    infoBox(s,f,252,"Import settings",
            "Reload /data/playtorrio/settings.json",
            loaded?"Imported":"Not found",selection==0);
    infoBox(s,f,352,"TorBox",
            cfg.torbox_api_key.empty()?"Add a token to your local JSON file":
                "TorBox key imported without exposing its value",
            cfg.torbox_api_key.empty()?"Missing key":"Key imported",selection==1);
    infoBox(s,f,452,"GitHub library",
            "Repository: "+cut(cfg.github_repo,44),
            cfg.github_token.empty()?"Missing key":"Key imported",selection==2);
    if(!error.empty())txt(s,f,cut(error,86),83,588,gold,1);
  }
}
std::string readConfig(){
  std::ifstream file("/data/playtorrio/settings.json");
  if(!file.good())return "";
  std::ostringstream out;out<<file.rdbuf();
  auto data=out.str();
  return data.size()>256*1024?"":data;
}
void footer(Scene2D& s,const Fonts& f,int tab,int focus){
  rect(s,0,681,1280,39,{16,19,28});
  circle(s,68,701,9,{61,158,117});
  txt(s,f,"X",62,706,paper,0);
  txt(s,f,tab==0?"Open selected section":"Select",89,708,muted,1);
  circle(s,291,701,9,{161,102,109});
  txt(s,f,"O",286,706,paper,0);
  txt(s,f,"Back",311,708,muted,1);
  txt(s,f,"L1 / R1  Switch tabs",472,708,muted,1);
  if(tab==4)txt(s,f,"Square  Reload settings",838,708,muted,1);
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

  int tab=0,focus=0,frame=0;
  uint32_t previous=0;
  while(true){
    OrbisPadData data={};
    if(pad>=0&&scePadReadState(pad,&data)>=0){
      uint32_t pressed=data.buttons&~previous;
      previous=data.buttons;
      if(pressed&ORBIS_PAD_BUTTON_R1){tab=(tab+1)%5;focus=0;}
      if(pressed&ORBIS_PAD_BUTTON_L1){tab=(tab+4)%5;focus=0;}
      if(pressed&ORBIS_PAD_BUTTON_RIGHT)focus=(focus+1)%(tab==0?5:3);
      if(pressed&ORBIS_PAD_BUTTON_LEFT)focus=(focus+(tab==0?4:2))%(tab==0?5:3);
      if(pressed&ORBIS_PAD_BUTTON_DOWN)focus=(focus+1)%(tab==0?5:3);
      if(pressed&ORBIS_PAD_BUTTON_UP)focus=(focus+(tab==0?4:2))%(tab==0?5:3);
      if(pressed&ORBIS_PAD_BUTTON_CROSS&&tab==0){tab=std::min(focus+1,4);focus=0;}
      if(pressed&ORBIS_PAD_BUTTON_CIRCLE){tab=0;focus=0;}
      if(pressed&ORBIS_PAD_BUTTON_SQUARE&&tab==4){
        ptv::Settings temp;std::string error;
        auto json=readConfig();
        if(!json.empty()&&ptv::import_settings(json,temp,error)){
          settings=temp;imported=true;parseError.clear();
        }else{imported=false;parseError=json.empty()?"Settings file not found":error;}
      }
    }
    if(tab==0)home(*screen,fonts,focus);
    else{
      screen->FrameBufferFill(bg);
      section(*screen,fonts,tab,settings,imported,focus,parseError);
    }
    nav(*screen,fonts,tab);
    footer(*screen,fonts,tab,focus);
    screen->SubmitFlip(frame);
    screen->FrameWait(frame);
    screen->FrameBufferSwap();
    ++frame;
    sceKernelUsleep(42000);
  }
}
