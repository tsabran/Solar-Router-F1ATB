#pragma once
#include <cctype>
#include <cstddef>
#include <cstring>
#include <string_view>

#ifndef STATIC_ASSET_TAG
#define STATIC_ASSET_TAG ""
#endif

inline const char *StaticAssetCacheControl(const char *version) {
  if (STATIC_ASSET_TAG[0] != '\0' && std::strcmp(version, STATIC_ASSET_TAG) == 0) {
    return "public, max-age=31536000, immutable";
  }
  return "no-store";
}

namespace StaticAssets {

inline std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  return text;
}

inline bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i]))) return false;
  }
  return true;
}

inline int Quality(std::string_view text) {
  if (text.empty() || (text[0] != '0' && text[0] != '1')) return -1;
  const bool one = text[0] == '1';
  if (text.size() == 1) return one ? 1000 : 0;
  if (text[1] != '.' || text.size() > 5) return -1;
  int quality = 0, place = 100;
  for (size_t i = 2; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9' || (one && text[i] != '0')) return -1;
    quality += (text[i] - '0') * place;
    place /= 10;
  }
  return one ? 1000 : quality;
}

inline bool AcceptsGzip(const char *header) {
  if (!header) return true;  // No Accept-Encoding field permits any coding (HTTP semantics).
  std::string_view remaining(header);
  int explicitQuality = -1, wildcardQuality = -1;
  while (!remaining.empty()) {
    const size_t comma = remaining.find(',');
    const auto item = Trim(remaining.substr(0, comma));
    if (comma == std::string_view::npos) remaining = {};
    else remaining.remove_prefix(comma + 1);
    const size_t semicolon = item.find(';');
    const auto coding = Trim(item.substr(0, semicolon));
    int quality = 1000;
    if (semicolon != std::string_view::npos) {
      const auto parameter = Trim(item.substr(semicolon + 1));
      const size_t equals = parameter.find('=');
      if (equals == std::string_view::npos ||
          !EqualsIgnoreCase(Trim(parameter.substr(0, equals)), "q")) quality = 0;
      else {
        quality = Quality(Trim(parameter.substr(equals + 1)));
        if (quality < 0) quality = 0;
      }
    }
    // An explicit gzip refusal overrides '*'; conflicting duplicates fail closed.
    int *target = nullptr;
    if (EqualsIgnoreCase(coding, "gzip")) target = &explicitQuality;
    else if (coding == "*") target = &wildcardQuality;
    if (target && (*target < 0 || quality < *target)) *target = quality;
  }
  return (explicitQuality >= 0 ? explicitQuality : wildcardQuality) > 0;
}

}  // namespace StaticAssets

struct StaticAssetPayload {
  size_t length;
  bool gzip;
};

#if __has_include("GeneratedStaticAssets.h")
#include "GeneratedStaticAssets.h"
#else
inline bool GetStaticAssetPayload(const char *data, StaticAssetPayload &payload) {
  if (!data) return false;
  payload = {std::strlen(data), false};
  return true;
}
#endif
