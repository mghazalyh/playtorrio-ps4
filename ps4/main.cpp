#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <cctype>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include "graphics.h"
#include "../src/core.hpp"

static const char* glyph(char c){
  // Compact 3-by-5 bitmap for dashboard headings, numbers and labels.
  const char* letters="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.-+";
  static const char* bitmap[]={
    "010101111101101","110101110101110","011100100100011",
    "110101101101110","111100110100111","111100110100100",
    "011100101101011","101101111101101","111010010010111",
    "001001001101010","101101110101101","100100100100111",
    "101111111101101","101111111111101","010101101101010",
    "110101110100100","010101101111011","110101110101101",
    "011100010001110","111010010010010","101101101101111",
    "101101101101010","101101111111101","101101010101101",
    "101101010010010","111001010100111",
    "111101101101111","010110010010111","110001111100111",
    "110001010001110","101101111001001","111100110001110",
    "011100111101111","111001010010010","111101111101111",
    "111101111001110","000010000010000","001001010100100",
    "000010000010000","000000000000010","000000111000000",
    "000010111010000"};
  auto n=std::string(letters).find((char)std::toupper((unsigned char)c));
  return n==std::string::npos?"000000000000000":bitmap[n];
}
static void print(Scene2D& scene,const std::string& msg,int x,int y,int scale,Color color){
  for(char c:msg){
    auto p=glyph(c);
    for(int row=0;row<5;row++)for(int col=0;col<3;col++)
      if(p[row*3+col]=='1')
        scene.DrawRectangle(x+col*scale,y+row*scale,scale,scale,color);
    x+=4*scale;
  }
}
static std::string config_text(){
  std::ifstream file("/data/playtorrio/settings.json");
  if(!file.good())return "";
  std::ostringstream out;out<<file.rdbuf();
  std::string data=out.str();
  return data.size()>256*1024?"":data;
}
static void draw(Scene2D& scene,int tab,const ptv::Settings& s,bool imported){
  const Color white={247,243,255},purple={153,119,239},muted={185,181,194};
  scene.FrameBufferFill({17,16,25});
  scene.DrawRectangle(0,0,1280,104,{34,27,54});
  print(scene,"PLAYTORRIO PS4",55,38,7,white);
  print(scene,"GOLDHEN 10.01",946,49,4,purple);
  const char* tabs[]={"HOME","ADDONS","TORBOX","LIBRARY","SETTINGS"};
  for(int i=0;i<5;i++){
    int x=50+i*246;
    scene.DrawRectangle(x,129,225,85,i==tab?purple:Color{44,39,59});
    print(scene,tabs[i],x+24,154,5,white);
  }
  if(tab==0){
    print(scene,"PLAYTORRIO NATIVE PS4",60,294,8,white);
    print(scene,"PROTOTYPE BUILD",60,390,6,purple);
    print(scene,"VIDEO PLAYBACK NOT YET CONNECTED",60,481,4,muted);
  }else if(tab==1){
    print(scene,"ADDON MANIFESTS",60,294,8,white);
    print(scene,"CONFIGURED ADDONS: "+std::to_string(s.addons.size()),60,392,5,purple);
    print(scene,"LIVE REQUESTS NOT READY",60,481,4,muted);
  }else if(tab==2){
    print(scene,"TORBOX",60,294,8,white);
    print(scene,s.torbox_api_key.empty()?"TOKEN MISSING":"TOKEN FOUND",60,392,5,purple);
    print(scene,"DEBRID PLAYER NOT READY",60,481,4,muted);
  }else if(tab==3){
    print(scene,"PRIVATE GITHUB LIBRARY",60,294,7,white);
    print(scene,"REPO: "+s.github_repo,60,392,4,purple);
    print(scene,s.github_token.empty()?"TOKEN MISSING":"TOKEN FOUND",60,453,4,muted);
    print(scene,"SYNC TRANSPORT NOT READY",60,509,4,muted);
  }else{
    print(scene,"IMPORT SETTINGS",60,294,8,white);
    print(scene,"/DATA/PLAYTORRIO/SETTINGS.JSON",60,383,4,white);
    print(scene,imported?"IMPORT SUCCESS":"NO VALID CONFIG",60,458,5,purple);
  }
  scene.DrawRectangle(0,660,1280,60,{34,27,54});
  print(scene,"L1 R1 CHANGE TAB   CIRCLE SETTINGS",53,682,4,white);
}
int main(){
  ptv::Settings settings;std::string error;
  std::string data=config_text();
  bool imported=!data.empty()&&ptv::import_settings(data,settings,error);
  // Secret data intentionally never logged.
  scePadInit();
  OrbisUserServiceInitializeParams param={};
  param.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;
  sceUserServiceInitialize(&param);
  int user=-1;sceUserServiceGetInitialUser(&user);
  int pad=scePadOpen(user,0,0,nullptr);
  Scene2D scene(1280,720,4);
  if(!scene.Init(0xC000000,2))return 1;
  int tab=0,frame=0;unsigned int previous=0;
  while(true){
    OrbisPadData reading={};
    if(pad>=0&&scePadReadState(pad,&reading)>=0){
      unsigned int edge=reading.buttons&~previous;
      previous=reading.buttons;
      if(edge&(ORBIS_PAD_BUTTON_R1|ORBIS_PAD_BUTTON_RIGHT))tab=(tab+1)%5;
      if(edge&(ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_LEFT))tab=(tab+4)%5;
      if(edge&ORBIS_PAD_BUTTON_CIRCLE)tab=4;
    }
    draw(scene,tab,settings,imported);
    scene.SubmitFlip(frame);scene.FrameWait(frame);scene.FrameBufferSwap();
    frame++;sceKernelUsleep(42000);
  }
  return 0;
}
