#pragma once
#include <cstddef>
#include <string>
#include "core.hpp"

namespace ptv {
enum class SettingsFileStatus {
  Loaded, Missing, Empty, TooLarge, InvalidEncoding, InvalidJson, ReadError
};
struct SettingsFileResult {
  SettingsFileStatus status = SettingsFileStatus::Missing;
  Settings settings;
  size_t bytes = 0;
  std::string message; // Never includes JSON source, tokens, or API response.
  bool ok() const { return status == SettingsFileStatus::Loaded; }
};
SettingsFileResult load_settings_file(const char* path);
const char* settings_file_status_name(SettingsFileStatus status);
}
