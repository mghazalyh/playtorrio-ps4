#pragma once
#include <string>
#include <vector>
#include <map>

namespace ptv {
struct Settings {
  std::vector<std::string> addons;
  std::string torbox_api_key;
  std::string github_token;
  std::string github_repo = "mghazalyh/torriodb";
  std::string github_branch = "main";
  std::string diagnostic_type = "movie";
  std::string diagnostic_id;  // Example: tt1234567 or tt1234567:1:1.
  int torbox_torrent_id = -1; // Optional existing TorBox torrent, no modifications.
  int torbox_file_id = -1;
  bool subtitles_english = true;
  bool audio_english = true;
};
struct Manifest { std::string id, name; bool streams = false; std::vector<std::string> types; };
struct Stream {
  std::string name, title, url, info_hash, addon;
  int file_index = -1;
  bool is_playable() const;
};
struct LibraryRecord {
  std::string key, item_json;
  bool deleted = false;
};
bool import_settings(const std::string& json, Settings& output, std::string& error);
bool normalize_manifest_url(const std::string& input, std::string& output);
bool parse_manifest(const std::string& json, Manifest& output, std::string& error);
std::string stream_url(const std::string& manifest_url, const std::string& type, const std::string& id);
std::vector<Stream> parse_streams(const std::string& json, const std::string& addon, std::string& error);
std::string torbox_request_url(int torrent_id, int file_id);
std::string torbox_extract_link(const std::string& json, std::string& error);
std::string github_get_url(const Settings& s);
std::string github_put_url(const Settings& s);
bool parse_library(const std::string& json, std::map<std::string, LibraryRecord>& records, std::string& error);
std::string encode_library(const std::map<std::string, LibraryRecord>& records);
std::map<std::string, LibraryRecord> merge_library(
    const std::map<std::string, LibraryRecord>& remote,
    const std::map<std::string, LibraryRecord>& pending);
std::string base64_encode(const std::string& input);
std::string base64_decode(const std::string& input);
std::string github_contents_update(const std::string& json, const std::string& sha, const std::string& branch);
}
