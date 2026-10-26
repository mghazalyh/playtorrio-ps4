#include "config_loader.hpp"
#include <cstdio>
#include <string>
#include <exception>
#include <cstring>

namespace ptv {
const char* settings_file_status_name(SettingsFileStatus s) {
  switch(s) {
    case SettingsFileStatus::Loaded: return "LOADED";
    case SettingsFileStatus::Missing: return "MISSING";
    case SettingsFileStatus::Empty: return "EMPTY";
    case SettingsFileStatus::TooLarge: return "TOO_LARGE";
    case SettingsFileStatus::InvalidEncoding: return "INVALID_ENCODING";
    case SettingsFileStatus::InvalidJson: return "INVALID_JSON";
    case SettingsFileStatus::ReadError: return "READ_ERROR";
  }
  return "UNKNOWN";
}
SettingsFileResult load_settings_file(const char* path) {
  SettingsFileResult result;
  if (!path || !*path) {
    result.status=SettingsFileStatus::ReadError;
    result.message="No settings file path";
    return result;
  }
  // libc FILE* is more portable across shadPS4's guest POSIX emulation than
  // constructing a C++ iostream/streambuf before VideoOut is initialized.
  std::FILE* in=std::fopen(path,"rb");
  if (!in) {
    result.status=SettingsFileStatus::Missing;
    result.message="settings.json not present";
    return result;
  }
  std::string data;
  constexpr size_t maxBytes=256*1024;
  char buffer[2048];
  bool readError=false;
  for (;;) {
    const size_t n=std::fread(buffer,1,sizeof(buffer),in);
    if (n) {
      if (data.size()+n>maxBytes) {
        result.status=SettingsFileStatus::TooLarge;
        result.message="settings.json exceeds 256 KiB";
        break;
      }
      data.append(buffer,n);
    }
    if (n<sizeof(buffer)) {
      if (std::ferror(in)) readError=true;
      break;
    }
  }
  std::fclose(in);
  result.bytes=data.size();
  if(result.status==SettingsFileStatus::TooLarge) return result;
  if(readError) {
    result.status=SettingsFileStatus::ReadError;
    result.message="Could not finish reading settings.json";
    return result;
  }
  if(data.empty()) {
    result.status=SettingsFileStatus::Empty;
    result.message="settings.json is empty";
    return result;
  }
  // Windows Notepad sometimes emits an UTF-8 BOM. Do not pass it to picojson.
  if(data.size()>=3 &&
     (unsigned char)data[0]==0xEF &&
     (unsigned char)data[1]==0xBB &&
     (unsigned char)data[2]==0xBF)
    data.erase(0,3);
  if(data.size()>=2 &&
      (((unsigned char)data[0]==0xFF && (unsigned char)data[1]==0xFE) ||
       ((unsigned char)data[0]==0xFE && (unsigned char)data[1]==0xFF))) {
    result.status=SettingsFileStatus::InvalidEncoding;
    result.message="Save settings.json as UTF-8, not UTF-16";
    return result;
  }
  if(data.find('\0')!=std::string::npos) {
    result.status=SettingsFileStatus::InvalidEncoding;
    result.message="settings.json contains NUL bytes; save as UTF-8";
    return result;
  }
  Settings parsed;
  std::string error;
  try {
    if (!import_settings(data,parsed,error)) {
      result.status=SettingsFileStatus::InvalidJson;
      result.message="settings.json is not valid PlayTorrio JSON";
      return result;
    }
  } catch(const std::exception&) {
    result.status=SettingsFileStatus::InvalidJson;
    result.message="Invalid settings data";
    return result;
  } catch(...) {
    result.status=SettingsFileStatus::InvalidJson;
    result.message="Unexpected settings parsing error";
    return result;
  }
  result.settings=std::move(parsed);
  result.status=SettingsFileStatus::Loaded;
  result.message="Local settings loaded";
  return result;
}
}
