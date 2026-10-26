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
#include "../src/config_loader.hpp"
#include "../src/catalog.hpp"
#include "diagnostics.hpp"
#include "video_player.hpp"
#include "source_resolver.hpp"
#include "catalog_online.hpp"
#include "poster_cache.hpp"
#include "../src/show_details.hpp"

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
void catalogTile(Scene2D& s,const Fonts& f,int x,int y,
                 const ptvcatalog::Item& item,bool selected,bool small=false){
  const int w=182,h=small?76:153;
  C tint=item.series?C{39,49,91}:C{64,42,70};
  round(s,x-2,y-2,w+4,h+4,10,selected?purple:line);
  round(s,x,y,w,h,9,tint);
  round(s,x+13,y+12,53,23,10,{25,28,42});
  txt(s,f,item.series?"SERIES":"MOVIE",x+20,y+28,{206,197,241},0);
  if(!small){
    // Data-backed cards: title, release year and rating are real TMDB fields.
    // Poster decoder/texture rendering remains a separate backend.
    rect(s,x+14,y+75,w-28,1,{94,84,123});
    txt(s,f,cut(item.title,16),x+13,y+105,paper,2);
    const std::string metadata=item.year+
      "  TMDB "+std::to_string((int)(item.rating*10)/10)+"."+
      std::to_string((int)(item.rating*10)%10);
    txt(s,f,cut(metadata,23),x+14,y+134,muted,0);
  }else{
    txt(s,f,cut(item.title,20),x+13,y+56,paper,1);
  }
  if(selected)rect(s,x+13,y+h-4,w-26,2,{242,220,255});
}

void artworkCard(Scene2D& scene,const Fonts& fonts,ps4art::Cache& art,
                 const ptvcatalog::Item& item,int x,int y,int width,int height,
                 bool selected,bool showTitle=true){
  round(scene,x-3,y-3,width+6,height+6,8,selected?purple:line);
  round(scene,x,y,width,height,6,item.series?C{35,46,74}:C{57,36,61});
  if(!art.draw(scene,item.poster_path,x+3,y+3,width-6,height-6)){
    // Placeholder is replaced asynchronously when remote TMDB artwork arrives.
    round(scene,x+12,y+12,width-24,25,8,{23,28,41});
    txt(scene,fonts,item.series?"TV":"MOVIE",x+21,y+30,{215,206,252},0);
    txt(scene,fonts,cut(item.title,15),x+14,y+height/2,paper,1);
  }
  if(selected){
    rect(scene,x+6,y+height-6,width-12,4,gold);
    round(scene,x+width-39,y+9,29,26,11,purple);
    txt(scene,fonts,"X",x+width-29,y+28,paper,0);
  }
  if(showTitle){
    round(scene,x+1,y+height-36,width-2,35,3,{11,15,25});
    txt(scene,fonts,cut(item.title,(size_t)std::max(10,width/11)),x+10,y+height-11,paper,1);
  }
}
void detailsPage(Scene2D& s,const Fonts& f,ps4art::Cache& art,
    const ptvcatalog::Item& item,const std::vector<ptvdetail::Season>& seasons,
    const std::vector<ptvdetail::Episode>& episodes,int seasonPos,int episodePos,
    int episodePage,bool loading,const std::string& error){
  rect(s,0,76,1280,607,{10,13,24});
  txt(s,f,item.series?"SERIES / EPISODES":"MOVIE / DETAILS",56,121,purple,1);
  artworkCard(s,f,art,item,57,153,223,323,false,false);
  txt(s,f,cut(item.title,36),322,169,paper,4);
  txt(s,f,item.year+"    TMDB "+std::to_string((int)(item.rating*10)/10)+"."+
    std::to_string((int)(item.rating*10)%10)+" / 10",323,206,gold,1);
  txt(s,f,cut(item.overview,93),322,248,muted,1);
  if(item.overview.size()>93)txt(s,f,cut(item.overview.substr(93),93),322,276,muted,1);
  if(item.overview.size()>186)txt(s,f,cut(item.overview.substr(186),93),322,304,muted,1);
  if(loading)txt(s,f,"Fetching seasons, episodes or streams...",326,349,gold,1);
  else if(!error.empty())txt(s,f,cut(error,86),327,345,gold,1);
  if(!item.series){
    round(s,320,393,570,96,16,{56,39,89});
    txt(s,f,"CHOOSE STREAM SOURCE",349,445,paper,3);
    txt(s,f,"Press X to compare available sources",323,538,muted,1);
  }else{
    txt(s,f,"SEASONS  |  L2 / R2 to change",323,357,paper,2);
    for(int i=0;i<(int)seasons.size()&&i<7;i++){
      // Window around active season prevents long-running TV shows overflowing.
      const int first=std::max(0,seasonPos-3);
      const int idx=first+i;
      if(idx>=(int)seasons.size())break;
      const bool active=idx==seasonPos;
      round(s,323+i*126,374,115,42,9,active?purple:C{32,38,57});
      txt(s,f,"Season "+std::to_string(seasons[idx].number),
          335+i*126,401,paper,1);
    }
    txt(s,f,"EPISODES  |  D-Pad to select  /  X to view sources",322,456,paper,1);
    for(int j=0;j<6;j++){
      int index=episodePage*6+j;
      if(index>=(int)episodes.size())break;
      const auto& e=episodes[index];
      const int y=470+j*30;
      round(s,324,y,862,27,5,index==episodePos?C{69,47,107}:C{25,30,47});
      txt(s,f,"E"+std::to_string(e.number)+"  "+cut(e.title,63),342,y+19,
          index==episodePos?paper:muted,1);
      if(index==episodePos)rect(s,325,y+2,3,23,gold);
    }
    txt(s,f,std::to_string(episodes.size())+" episodes  /  page "+
        std::to_string(episodePage+1),1028,453,green,0);
  }
  txt(s,f,"Circle: Back to browsing",70,580,muted,1);
}
std::string sourceLabel(const ptv::Stream& stream){
  std::string text=stream.name.empty()?stream.title:stream.name+"  "+stream.title;
  std::string safe;
  for(unsigned char c:text){
    if(safe.size()>=110)break;
    if(c>=32&&c<127)safe.push_back((char)c);
    else if(c=='\n'||c=='\r')safe.push_back(' ');
  }
  return safe.empty()?"Unnamed stream source":safe;
}
void sourcePage(Scene2D& s,const Fonts& f,const ptvcatalog::Item& item,
   const ps4source::Choices& choices,int focus,int page,bool fetching,
   const std::string& error){
  rect(s,0,76,1280,608,{9,13,23});
  txt(s,f,"STREAM SOURCES  /  "+cut(item.title,39),57,126,paper,4);
  txt(s,f,"Choose a source. Direct HTTPS or TorBox; higher-quality matches listed first.",
      61,166,muted,1);
  if(fetching)txt(s,f,"Searching your installed Stremio addons...",61,245,gold,2);
  else if(choices.entries.empty())txt(s,f,"No compatible sources. "+cut(error,63),61,245,gold,1);
  for(int row=0;row<7;row++){
    const int index=page*7+row;
    if(index>=(int)choices.entries.size())break;
    const auto& c=choices.entries[index];
    const int y=190+row*62;
    round(s,52,y,1176,54,10,index==focus?C{65,46,96}:card);
    if(index==focus)rect(s,54,y+7,4,40,gold);
    txt(s,f,cut(sourceLabel(c.stream),86),73,y+23,paper,1);
    txt(s,f,c.direct?"DIRECT HTTPS":"TORBOX / ON-DEMAND",74,y+44,
        c.direct?green:gold,0);
    txt(s,f,c.direct?"READY":"RESOLVE WHEN SELECTED",990,y+36,
        c.direct?green:muted,0);
  }
  txt(s,f,"UP / DOWN: source   L2/R2: page   X: play   Circle: episodes/details",
      62,671,muted,0);
}
void home(Scene2D& s,const Fonts& f,ps4art::Cache& art,int cardFocus,
          const ps4diag::Summary& diag,const ptvcatalog::Catalog& catalog){
  if(!catalog.loaded){
    stripe(s);
    round(s,51,103,139,23,11,{67,49,91});
    txt(s,f,"PLAYTORRIO FOR TV",62,120,{219,206,255},0);
    txt(s,f,"Your next watch.",49,194,paper,5);
    txt(s,f,"Fetching TMDB natively; Windows HostTools remains optional.",52,232,muted,1);
    txt(s,f,"HOME       DISCOVER       MY LIST       ADDONS",53,267,{197,177,232},0);
    txt(s,f,"Browse",52,342,paper,3);
    for(int j=0;j<6;j++)featureCard(s,f,53+j*198,368,j,j==cardFocus);
    txt(s,f,"Quick access",52,558,paper,3);
    for(int j=0;j<6;j++)miniCard(s,f,53+j*198,570,j,j+6==cardFocus,diag);
    return;
  }
  stripe(s);
  const auto& featured=!catalog.tv.empty()?catalog.tv.front():catalog.movies.front();
  round(s,51,95,150,24,12,{69,43,95});
  txt(s,f,"TRENDING ON TV",67,112,paper,0);
  txt(s,f,cut(featured.title,32),52,180,paper,5);
  txt(s,f,cut(featured.overview,90),53,218,muted,1);
  txt(s,f,cut(featured.overview.size()>90?featured.overview.substr(90):"",90),53,244,muted,1);
  txt(s,f,"SELECT A POSTER TO OPEN SHOW DETAILS",53,279,gold,0);
  txt(s,f,"Trending series",52,341,paper,3);
  txt(s,f,std::to_string(catalog.tv.size())+" shows",1107,341,green,1);
  for(int j=0;j<6&&j<(int)catalog.tv.size();j++)
    artworkCard(s,f,art,catalog.tv[j],54+j*199,363,170,202,j==cardFocus);
  txt(s,f,"Popular movies",52,605,paper,3);
  for(int j=0;j<6&&j<(int)catalog.movies.size();j++){
    round(s,54+j*199,615,170,37,6,j+6==cardFocus?C{64,43,96}:card);
    txt(s,f,cut(catalog.movies[j].title,17),64+j*199,639,paper,0);
  }
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
             const ps4diag::Summary& diag,const ptvcatalog::Catalog& catalog,
             bool tvMode,int page,ps4art::Cache& art){
  // Six complete, compact rows on a 720p television (was three).
  const int rows[]={173,255,337,419,501,583};
  if(tab==1){
    if(catalog.loaded){
      const auto& items=tvMode?catalog.tv:catalog.movies;
      heading(s,f,tvMode?"Discover  /  TV SERIES":"Discover  /  MOVIES",
        "LEFT / RIGHT: type     L2 / R2: page     X: details");
      for(int j=0;j<6;j++){
        const int index=page*6+j;
        if(index>=(int)items.size())break;
        artworkCard(s,f,art,items[index],54+j*199,191,171,267,j==selection);
      }
      const int selected=page*6+selection;
      if(selected>=0&&selected<(int)items.size()){
        const auto& item=items[selected];
        round(s,54,492,1171,126,13,{22,28,47});
        txt(s,f,cut(item.title,52),72,525,paper,3);
        txt(s,f,item.year+"   TMDB "+std::to_string((int)item.rating)+"/10",
            956,526,gold,1);
        txt(s,f,cut(item.overview,118),72,558,muted,1);
        if(item.overview.size()>118)
          txt(s,f,cut(item.overview.substr(118),115),72,587,muted,1);
      }
      txt(s,f,"PAGE "+std::to_string(page+1)+" / "+
           std::to_string(std::max(1,((int)items.size()+5)/6)),
           1037,159,green,1);
    }else{
      heading(s,f,"Discover","Awaiting real TMDB TV and movie catalogs");
      infoBox(s,f,rows[0],"No catalog downloaded",
              "Native TMDB loading; HostTools is the offline fallback","Not loaded",true);
      infoBox(s,f,rows[1],"Windows shared path",
              "shadPS4/data/playtorrio/catalog.json","Auto-detected");
      infoBox(s,f,rows[2],"TMDB API key",
              cfg.tmdb_api_key.empty()?"Missing in settings.json":"Loaded in settings.json",
              cfg.tmdb_api_key.empty()?"Missing":"Ready");
      infoBox(s,f,rows[3],"Native shadPS4 networking",
              "Press Triangle: check HTTP status and errno","See log");
      if(!catalog.error.empty())
        txt(s,f,cut(catalog.error,94),73,538,gold,1);
    }
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
  // Delay settings I/O until the UI is drawing frames. A malformed file or
  // shadPS4 guest filesystem stall must not black-screen the whole app.
  ptv::Settings settings;std::string parseError="Loading local settings...";
  bool imported=false;
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
  ps4art::Cache posters;
  int panel=0; // 0 browse, 1 title details/episodes, 2 source picker
  ptvcatalog::Item selectedItem;
  std::string detailImdb,detailMessage;
  std::vector<ptvdetail::Season> seasons;
  std::vector<ptvdetail::Episode> episodes;
  int seasonFocus=0,episodeFocus=0,sourceFocus=0;
  ps4source::Choices sourceChoices;
  struct DetailResult {
    std::string imdb;
    std::vector<ptvdetail::Season> seasons;
    std::vector<ptvdetail::Episode> episodes;
    bool seasonOnly=false;
    std::string error;
  };
  std::mutex detailMutex;
  DetailResult pendingDetail;
  bool detailReady=false;
  std::atomic<bool> detailBusy{false};
  std::mutex sourcesMutex;
  ps4source::Choices pendingChoices;
  bool sourcesReady=false;
  std::atomic<bool> sourcesBusy{false};
  bool viewing=false;
  bool overlay=true;
  std::atomic<bool> sourceBusy{false};
  std::mutex sourceMutex;
  std::string preparedUrl, preparedError;
  bool sourceReady=false;
  std::atomic<bool> settingsBusy{false};
  std::mutex settingsMutex;
  ptv::SettingsFileResult pendingSettings;
  bool settingsReady=false;
  bool initialSettingsRequested=false;
  ptvcatalog::Catalog catalog;
  ptvcatalog::Catalog pendingCatalog;
  std::mutex catalogMutex;
  std::atomic<bool> catalogBusy{false};
  bool catalogReady=false;
  std::atomic<bool> nativeBusy{false};
  bool nativeCatalogActive=false;
  bool pendingNative=false;
  auto requestNative=[&](const ptv::Settings& config){
    if(config.tmdb_api_key.empty()||nativeBusy.exchange(true))return;
    ps4diag::note("CATALOG","BEGIN native TMDB request");
    std::thread([&,config](){
      ptvcatalog::Catalog remote;
      try{remote=ptvcatalog::fetch_online(config);}
      catch(const std::exception&){remote.error="Native TMDB exception";}
      catch(...){remote.error="Native TMDB unexpected failure";}
      {
        std::lock_guard<std::mutex> lock(catalogMutex);
        pendingCatalog=std::move(remote);
        pendingNative=true;
        catalogReady=true;
      }
      nativeBusy.store(false);
    }).detach();
  };
  bool catalogTv=true;
  int catalogPage=0;
  auto requestCatalog=[&](){
    if(catalogBusy.exchange(true))return;
    std::thread([&](){
      auto next=ptvcatalog::load_file("/data/playtorrio/catalog.json");
      {
        std::lock_guard<std::mutex> lock(catalogMutex);
        pendingCatalog=std::move(next);
        pendingNative=false;
        catalogReady=true;
      }
      catalogBusy.store(false);
    }).detach();
  };
  auto requestSettings=[&](bool allowUsb){
    if(settingsBusy.exchange(true))return;
    ps4diag::note("SETTINGS","BEGIN deferred settings read (no secrets logged)");
    std::thread([&,allowUsb](){
      auto result=ptv::load_settings_file("/data/playtorrio/settings.json");
      if(result.status==ptv::SettingsFileStatus::Missing && allowUsb){
        ptv::Settings usbSettings;
        std::string message;
        if(importUsb(usbSettings,message)){
          result.settings=std::move(usbSettings);
          result.status=ptv::SettingsFileStatus::Loaded;
          result.message="USB settings installed";
        }
      }
      ps4diag::note("SETTINGS",std::string("END status=")+
        ptv::settings_file_status_name(result.status)+
        " bytes="+std::to_string(result.bytes)+
        " addons="+std::to_string(result.ok()?result.settings.addons.size():0)+
        " tmdb_set="+std::to_string(result.ok()&&!result.settings.tmdb_api_key.empty())+
        " github_enabled="+std::to_string(result.ok()&&result.settings.github_sync_enabled));
      {
        std::lock_guard<std::mutex> lock(settingsMutex);
        pendingSettings=std::move(result);
        settingsReady=true;
      }
      settingsBusy.store(false);
    }).detach();
  };
  auto requestPlayback=[&](std::string mediaType,std::string mediaId){
    if(viewing||sourceBusy.exchange(true))return;
    preparedError.clear();
    const auto config=settings;
    ps4diag::note("CONTROL","Requesting playable video from local sources");
    std::thread([&,config,mediaType,mediaId](){
      ps4source::Selection found;
      try{
        std::string resolved=mediaId;
        if(mediaId.rfind("tmdb:",0)==0){
          const std::string value=mediaId.substr(5);
          bool valid=!value.empty()&&value.size()<11&&
            std::all_of(value.begin(),value.end(),
              [](unsigned char c){return c>='0'&&c<='9';});
          int number=0;
          if(valid){
            try{number=std::stoi(value);}catch(...){number=0;}
          }
          if(number>0){
            resolved=ptvcatalog::imdb_for_tmdb(config,mediaType=="series",number);
            if(!resolved.empty()&&mediaType=="series")resolved+=":1:1";
          }else resolved.clear();
          if(resolved.empty()){
            found.reason="TMDB has no usable IMDb ID for this title";
            ps4diag::note("CATALOG","FAIL title IMDb lookup");
          }else ps4diag::note("CATALOG","PASS title IMDb lookup");
        }
        if(!resolved.empty())found=ps4source::resolve(config,mediaType,resolved);
      }catch(const std::exception&){
        found.reason="Unexpected source resolution failure";
        ps4diag::note("PLAYBACK","FAIL unexpected source resolution exception");
      }catch(...){
        found.reason="Unexpected source resolution error";
        ps4diag::note("PLAYBACK","FAIL unknown source resolution error");
      }
      {
        std::lock_guard<std::mutex> guard(sourceMutex);
        preparedUrl=std::move(found.url);
        preparedError=found.reason;
        sourceReady=true;
      }
      sourceBusy.store(false);
    }).detach();
  };

  auto openDetails=[&](const ptvcatalog::Item& item){
    if(viewing||detailBusy.exchange(true))return;
    selectedItem=item;detailImdb=item.imdb_id;
    seasons.clear();episodes.clear();sourceChoices={};
    seasonFocus=0;episodeFocus=0;sourceFocus=0;panel=1;
    detailMessage="Loading details...";
    const auto config=settings;
    std::thread([&,item,config](){
      DetailResult result;
      try{
        result.imdb=item.imdb_id.empty()?
          ptvcatalog::imdb_for_tmdb(config,item.series,item.tmdb_id):item.imdb_id;
        if(item.series){
          if(!ptvcatalog::fetch_seasons(config,item.tmdb_id,result.seasons))
            result.error="Could not load TV seasons from TMDB";
          else if(!result.seasons.empty() &&
                  !ptvcatalog::fetch_episodes(config,item.tmdb_id,
                    result.seasons.front().number,result.episodes))
            result.error="Could not load episodes from TMDB";
        }
        if(result.imdb.empty())result.error="IMDb identifier unavailable for source lookup";
      }catch(...){result.error="Unexpected TMDB details error";}
      {std::lock_guard<std::mutex> lock(detailMutex);
       pendingDetail=std::move(result);detailReady=true;}
      detailBusy.store(false);
    }).detach();
  };
  auto chooseSeason=[&](int focusSeason){
    if(focusSeason<0||focusSeason>=(int)seasons.size()||
       detailBusy.exchange(true))return;
    seasonFocus=focusSeason;episodeFocus=0;
    episodes.clear();detailMessage="Loading season episodes...";
    const auto config=settings;
    const int tmdbId=selectedItem.tmdb_id;
    const int season=seasons[seasonFocus].number;
    std::thread([&,config,tmdbId,season](){
      DetailResult result;result.seasonOnly=true;
      try{
        if(!ptvcatalog::fetch_episodes(config,tmdbId,season,result.episodes))
          result.error="Could not load this season from TMDB";
      }catch(...){result.error="Episode network error";}
      {std::lock_guard<std::mutex> lock(detailMutex);
       pendingDetail=std::move(result);detailReady=true;}
      detailBusy.store(false);
    }).detach();
  };
  auto openSources=[&](const std::string& videoId){
    if(videoId.empty()){detailMessage="No IMDb ID for source lookup";return;}
    if(sourcesBusy.exchange(true))return;
    panel=2;sourceFocus=0;sourceChoices={};detailMessage.clear();
    const auto config=settings;
    const auto type=selectedItem.series?"series":"movie";
    std::thread([&,config,type,videoId](){
      ps4source::Choices result;
      try{result=ps4source::list_sources(config,type,videoId);}
      catch(...){result.error="Source lookup failed";}
      {std::lock_guard<std::mutex> lock(sourcesMutex);
       pendingChoices=std::move(result);sourcesReady=true;}
      sourcesBusy.store(false);
    }).detach();
  };
  auto playSelected=[&](){
    if(viewing||sourceBusy.exchange(true))return;
    if(sourceFocus<0||sourceFocus>=(int)sourceChoices.entries.size()){
      sourceBusy.store(false);return;
    }
    const auto config=settings;
    const auto candidate=sourceChoices.entries[sourceFocus];
    detailMessage="Preparing selected source...";
    ps4diag::note("CONTROL","User chose a source from stream picker");
    std::thread([&,config,candidate](){
      ps4source::Selection resolved;
      try{resolved=ps4source::resolve_selected(config,candidate);}
      catch(...){resolved.reason="Selected source resolver failed";}
      {std::lock_guard<std::mutex> lock(sourceMutex);
       preparedUrl=std::move(resolved.url);
       preparedError=resolved.reason;
       sourceReady=true;}
      sourceBusy.store(false);
    }).detach();
  };
  uint32_t previous=0;
  ps4diag::note("BOOT","PASS entered main PS4 render loop");
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
      // Title and source panels have their own focus; no global tab movement,
      // background catalog refresh or diagnostic trigger steals a button.
      if(!viewing&&panel==1){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){panel=0;detailMessage.clear();}
        else if(selectedItem.series){
          if(pressed&ORBIS_PAD_BUTTON_L2 && !seasons.empty())
            chooseSeason((seasonFocus+(int)seasons.size()-1)%seasons.size());
          if(pressed&ORBIS_PAD_BUTTON_R2 && !seasons.empty())
            chooseSeason((seasonFocus+1)%seasons.size());
          if(!episodes.empty()){
            if(pressed&ORBIS_PAD_BUTTON_DOWN)episodeFocus=(episodeFocus+1)%episodes.size();
            if(pressed&ORBIS_PAD_BUTTON_UP)episodeFocus=(episodeFocus+(int)episodes.size()-1)%episodes.size();
            if(pressed&ORBIS_PAD_BUTTON_CROSS&&!detailBusy.load()){
              const auto& episode=episodes[episodeFocus];
              openSources(ptvdetail::stremio_episode_id(
                detailImdb,episode.season,episode.number));
            }
          }
        }else if(pressed&ORBIS_PAD_BUTTON_CROSS && !detailBusy.load())
          openSources(detailImdb);
        pressed=0;
      }else if(!viewing&&panel==2){
        const int count=(int)sourceChoices.entries.size();
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){panel=1;detailMessage.clear();}
        else if(count>0){
          if(pressed&ORBIS_PAD_BUTTON_DOWN)sourceFocus=(sourceFocus+1)%count;
          if(pressed&ORBIS_PAD_BUTTON_UP)sourceFocus=(sourceFocus+count-1)%count;
          if(pressed&ORBIS_PAD_BUTTON_R2)sourceFocus=std::min(count-1,(sourceFocus/7+1)*7);
          if(pressed&ORBIS_PAD_BUTTON_L2)sourceFocus=std::max(0,(sourceFocus/7-1)*7);
          if(pressed&ORBIS_PAD_BUTTON_CROSS&&!sourcesBusy.load())playSelected();
        }
        pressed=0;
      }
      if((pressed&ORBIS_PAD_BUTTON_SQUARE) && tab!=4){
        requestCatalog();
        requestNative(settings);
        ps4diag::note("CATALOG","User requested native and cached TMDB refresh");
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
        if(pressed&ORBIS_PAD_BUTTON_CROSS && catalog.loaded){
          const bool isTv=focus<6;
          const auto& items=isTv?catalog.tv:catalog.movies;
          if(focus%6<(int)items.size())openDetails(items[focus%6]);
        }else if(pressed&ORBIS_PAD_BUTTON_CROSS){
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
      }else if(tab==1 && catalog.loaded){
        if(pressed&ORBIS_PAD_BUTTON_RIGHT){catalogTv=false;catalogPage=0;focus=0;}
        if(pressed&ORBIS_PAD_BUTTON_LEFT){catalogTv=true;catalogPage=0;focus=0;}
        const auto& collection=catalogTv?catalog.tv:catalog.movies;
        const int pages=std::max(1,((int)collection.size()+5)/6);
        if(pressed&ORBIS_PAD_BUTTON_R2){catalogPage=(catalogPage+1)%pages;focus=0;}
        if(pressed&ORBIS_PAD_BUTTON_L2){catalogPage=(catalogPage+pages-1)%pages;focus=0;}
        if(pressed&ORBIS_PAD_BUTTON_DOWN)
          focus=(focus+1)%6;
        if(pressed&ORBIS_PAD_BUTTON_UP)
          focus=(focus+5)%6;
      }else{
        if(pressed&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_DOWN))
          focus=(focus+1)%6;
        if(pressed&(ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_UP))
          focus=(focus+5)%6;
      }
      if(pressed&ORBIS_PAD_BUTTON_CIRCLE){tab=0;focus=0;}
      if(!running.load() && ((pressed&ORBIS_PAD_BUTTON_SQUARE && tab==4)||
         (pressed&ORBIS_PAD_BUTTON_CROSS && tab==4 && focus==0 && tabBeforeSelection==4))){
        requestSettings(true);
        parseError="Reloading settings in the background...";
      }
      if(pressed&ORBIS_PAD_BUTTON_CROSS && tab==1 && tabBeforeSelection==1){
        if(catalog.loaded){
          const auto& list=catalogTv?catalog.tv:catalog.movies;
          const int index=catalogPage*6+focus;
          if(index>=0 && index<(int)list.size()){
            const auto& item=list[index];
            openDetails(item);
          }
        }else requestPlayback(settings.diagnostic_type,settings.diagnostic_id);
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
    if(frame==2 && !initialSettingsRequested){
      initialSettingsRequested=true;
      requestSettings(false);
    }
    // Windows companion updater writes atomically. Reload without blocking
    // frame presentation and without reloading the user's secret settings.
    if(frame==10 || (frame>10 && frame%360==10))requestCatalog();
    {
      std::lock_guard<std::mutex> guard(catalogMutex);
      if(catalogReady){
        if(pendingCatalog.loaded && (pendingNative||!nativeCatalogActive)){
          if(pendingNative)nativeCatalogActive=true;
          if(!catalog.loaded || catalog.updated!=pendingCatalog.updated){
            ps4diag::note("CATALOG","PASS local TMDB TV="+
              std::to_string(pendingCatalog.tv.size())+" movies="+
              std::to_string(pendingCatalog.movies.size()));
          }
          catalog=std::move(pendingCatalog);
        }else if(!catalog.loaded){
          catalog.error=pendingCatalog.error;
        }
        if(pendingNative && !pendingCatalog.loaded)
          ps4diag::note("CATALOG","FAIL "+pendingCatalog.error);
        catalogReady=false;
      }
    }
    {
      std::lock_guard<std::mutex> lock(settingsMutex);
      if(settingsReady){
        if(pendingSettings.ok()){
          settings=std::move(pendingSettings.settings);
          imported=true;
          requestNative(settings);
          parseError="Local settings loaded";
        }else{
          // An invalid file never replaces the last working configuration.
          // The app remains navigable for a manual reload after editing.
          parseError=pendingSettings.message;
        }
        settingsReady=false;
      }
    }
    // Publish native season/episode and source results only on the main thread.
    {std::lock_guard<std::mutex> lock(detailMutex);
      if(detailReady){
        if(!pendingDetail.seasonOnly){
          detailImdb=std::move(pendingDetail.imdb);
          seasons=std::move(pendingDetail.seasons);
        }
        episodes=std::move(pendingDetail.episodes);
        episodeFocus=0;
        detailMessage=std::move(pendingDetail.error);
        ps4diag::note("DETAIL",std::string("Loaded seasons=")+
          std::to_string(seasons.size())+" episodes="+std::to_string(episodes.size())+
          " imdb="+std::to_string(!detailImdb.empty()));
        detailReady=false;
      }
    }
    {std::lock_guard<std::mutex> lock(sourcesMutex);
      if(sourcesReady){
        sourceChoices=std::move(pendingChoices);
        detailMessage=sourceChoices.error;
        sourcesReady=false;
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
        if(panel==2)detailMessage=openingStatus;
        ps4diag::note("PLAYBACK","FAIL resolution did not yield a supported stream");
      }else{
        std::string failure;
        if(player.start(openingUrl,failure)){
          viewing=true;overlay=true;
          detailMessage.clear();
          ps4diag::note("PLAYBACK","START AvPlayer video session");
        }else{
          parseError=failure;
          if(panel==2)detailMessage=failure;
        }
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
          txt(*screen,fonts,"Buffering video... "+std::to_string(player.bufferingSeconds())+"s",65,135,gold,2);
        if(player.state()==ps4video::Player::Status::Ended)
          txt(*screen,fonts,"Stream ended. Circle to exit.",65,135,gold,2);
        if(player.state()==ps4video::Player::Status::Failed)
          txt(*screen,fonts,"No decoded video. Check logs; Circle to exit.",65,135,gold,2);
      }
      (void)frameDrawn;
    }else{
      // Always paint the complete background, including a fresh initial
      // frame. Never reuse uninitialized video-out memory between flips.
      screen->FrameBufferFill(bg);
      if(panel==1){
        detailsPage(*screen,fonts,posters,selectedItem,seasons,episodes,seasonFocus,
                    episodeFocus,episodeFocus/6,detailBusy.load(),detailMessage);
      }else if(panel==2){
        sourcePage(*screen,fonts,selectedItem,sourceChoices,sourceFocus,sourceFocus/7,
                   sourcesBusy.load()||sourceBusy.load(),detailMessage);
      }else{
        if(tab==0)home(*screen,fonts,posters,focus,snapshot,catalog);
        else section(*screen,fonts,tab,settings,imported,focus,parseError,snapshot,
                     catalog,catalogTv,catalogPage,posters);
      }
      nav(*screen,fonts,tab);
      if(!panel){
        diagnosticBand(*screen,fonts,snapshot,completed,logging,running.load());
        footer(*screen,fonts,tab,focus);
      }
      if(sourceBusy.load()){
        round(*screen,240,275,800,100,12,{32,30,47});
        txt(*screen,fonts,"Preparing selected playback source...",269,335,paper,2);
      }else if(!panel&&!parseError.empty()){
        txt(*screen,fonts,cut(parseError,78),65,641,gold,0);
      }
    }
    if(frame==0)ps4diag::note("RENDER","Drawing first screen completed; submitting video flip");
    screen->SubmitFlip(frame);
    if(frame==0)ps4diag::note("RENDER","First flip submitted; waiting for video output");
    screen->FrameWait(frame);
    if(frame==0)ps4diag::note("RENDER","First frame wait completed; swapping buffers");
    screen->FrameBufferSwap();
    ++frame;
    if(frame==30)ps4diag::note("RENDER","PASS first 30 frames of UI presented");
    sceKernelUsleep(42000);
  }
}
