#include "native_net.hpp"
#include <orbis/Http.h>
#include <orbis/Ssl.h>
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <orbis/_types/http.h>
#include <cstdint>
#include <algorithm>
#include <string>

namespace ps4net {
static int networkPool=0,ssl=0,http=0;
bool init(int& error) {
  error=0;
  if(http>0)return true;
  if((error=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET))<0)return false;
  if((error=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP))<0)return false;
  if((error=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL))<0)return false;
  if((error=sceNetInit())<0)return false;
  networkPool=sceNetPoolCreate("PlayTorrioNet",256*1024,0);
  if(networkPool<0){error=networkPool;networkPool=0;return false;}
  ssl=sceSslInit(256*1024);
  if(ssl<0){error=ssl;ssl=0;return false;}
  http=sceHttpInit(networkPool,ssl,512*1024);
  if(http<0){error=http;http=0;return false;}
  return true;
}
Response request(const std::string& url,const std::string& method,
    const std::map<std::string,std::string>& headers,
    const std::string& body,size_t limit) {
  Response result;
  // Never send private keys to plaintext HTTP, embedded URL credentials,
  // or malformed endpoints. Do not log full URL/query or returned body.
  if(url.rfind("https://",0)!=0||url.size()>4096||
      url.find_first_of("\r\n\t")!=std::string::npos) {
    result.platform_error=-1;return result;
  }
  auto hostEnd=url.find('/',8);
  if(url.find('@',8)!=(std::string::npos) &&
      (hostEnd==std::string::npos||url.find('@',8)<hostEnd)){
    result.platform_error=-2;return result;
  }
  int err=0;
  if(!init(err)){result.platform_error=err;return result;}
  int tmpl=sceHttpCreateTemplate(http,"PlayTorrio-PS4/0.4",ORBIS_HTTP_VERSION_1_1,0);
  if(tmpl<0){result.platform_error=tmpl;return result;}
  int conn=sceHttpCreateConnectionWithURL(tmpl,url.c_str(),false),req=-1;
  if(conn<0){result.platform_error=conn;goto done;}
  {
    int m=method=="PUT"?ORBIS_METHOD_PUT:method=="POST"?ORBIS_METHOD_POST:method=="HEAD"?ORBIS_METHOD_HEAD:ORBIS_METHOD_GET;
    req=sceHttpCreateRequestWithURL(conn,m,url.c_str(),body.size());
  }
  if(req<0){result.platform_error=req;goto done;}
  sceHttpSetConnectTimeOut(req,7*1000000);
  sceHttpSetResolveTimeOut(req,7*1000000);
  sceHttpSetSendTimeOut(req,10*1000000);
  for(const auto& h:headers) {
    if(h.first.find_first_of("\r\n")!=std::string::npos ||
       h.second.find_first_of("\r\n")!=std::string::npos)continue;
    const int ret=sceHttpAddRequestHeader(req,h.first.c_str(),h.second.c_str(),0);
    if(ret<0){result.platform_error=ret;goto done;}
  }
  {
    const int code=sceHttpSendRequest(req,body.empty()?nullptr:body.data(),body.size());
    if(code<0){result.platform_error=code;goto done;}
  }
  if(sceHttpGetStatusCode(req,&result.status)<0){result.platform_error=-3;goto done;}
  if(method=="HEAD")goto done;
  while(result.body.size()<limit) {
    char buffer[8192];
    uint32_t ask=(uint32_t)std::min((size_t)sizeof(buffer),limit-result.body.size());
    int read=sceHttpReadData(req,buffer,ask);
    if(read<0){result.platform_error=read;break;}
    if(!read)break;
    result.body.append(buffer,read);
  }
  if(result.body.size()==limit)result.truncated=true;
done:
  if(req>=0)sceHttpDeleteRequest(req);
  if(conn>=0)sceHttpDeleteConnection(conn);
  sceHttpDeleteTemplate(tmpl);
  return result;
}
void shutdown(){
  if(http>0){sceHttpTerm(http);http=0;}
  if(ssl>0){sceSslTerm();ssl=0;}
  if(networkPool>0){sceNetPoolDestroy(networkPool);networkPool=0;}
}
}
