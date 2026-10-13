#pragma once

#include <ArduinoJson.h>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include "EnvoyReadObserver.h"


inline bool EnvoyEnergySampleFresh(const EnvoyEnergySample& sample, uint64_t epoch,
                                  double maxPowerW) {
  return epoch > 1000000000 && sample.stamp > 0 && sample.stamp <= epoch + 2 &&
         sample.stamp + 3 >= epoch && std::isfinite(sample.powerW) &&
         std::abs(sample.powerW) <= maxPowerW;
}

inline bool ParseEnvoyReadingSample(const char* json, EnvoyEnergySample& sample) {
  JsonDocument filter;
  filter["timestamp"] = true;
  filter["activePower"] = true;
  JsonDocument reading;
  if (deserializeJson(reading, json, DeserializationOption::Filter(filter))) return false;
  if (!reading["timestamp"].is<uint64_t>() || !reading["activePower"].is<double>())
    return false;
  sample.stamp = reading["timestamp"].as<uint64_t>();
  sample.powerW = reading["activePower"].as<double>();
  return sample.stamp > 0 && std::isfinite(sample.powerW);
}

inline bool ParseEnvoyPhasePower(const char* json, double& powerW) {
  JsonDocument filter;
  filter["activePower"] = true;
  JsonDocument phase;
  if (deserializeJson(phase, json, DeserializationOption::Filter(filter)) ||
      !phase["activePower"].is<double>()) return false;
  powerW = phase["activePower"].as<double>();
  return std::isfinite(powerW);
}

// Délimite l'objet compteur jusqu'avant "channels" (dont le tableau suit le '[' consommé).
inline bool EnvoyReadingHeaderBounds(const char* header, const char*& start, size_t& length) {
  start = std::strchr(header, '{');
  const char* channels = std::strstr(header, "\"channels\"");
  if (!start || !channels || channels <= start) return false;
  const char* suffix = channels + std::strlen("\"channels\"");
  while (*suffix && std::isspace(static_cast<unsigned char>(*suffix))) ++suffix;
  if (*suffix++ != ':') return false;
  while (*suffix && std::isspace(static_cast<unsigned char>(*suffix))) ++suffix;
  if (*suffix && *suffix != '[') return false;
  length = static_cast<size_t>(channels - start);
  while (length && (start[length - 1] == ' ' || start[length - 1] == '\r' ||
                    start[length - 1] == '\n' || start[length - 1] == '\t'))
    --length;
  if (length && start[length - 1] == ',') --length;
  return true;
}

inline bool EnvoyReadingHeaderJson(const char* header, char* json, size_t capacity) {
  const char* start;
  size_t length;
  if (!EnvoyReadingHeaderBounds(header, start, length) || length + 2 > capacity) return false;
  std::memcpy(json, start, length);
  json[length] = '}';
  json[length + 1] = '\0';
  return true;
}

inline bool ParseEnvoyReadingHeader(const char* header, EnvoyEnergySample& sample) {
  char json[1024];
  return EnvoyReadingHeaderJson(header, json, sizeof(json)) &&
         ParseEnvoyReadingSample(json, sample);
}

inline bool ParseEnvoyEnergySample(const char* json, EnvoyEnergySample& sample) {
  JsonDocument filter;
  filter[0]["reportType"] = true;
  filter[0]["createdDate"] = true;
  filter[0]["cumulative"]["actPower"] = true;
  JsonDocument reports;
  if (deserializeJson(reports, json, DeserializationOption::Filter(filter))) return false;
  if (!reports.is<JsonArray>()) return false;
  for (JsonObject report : reports.as<JsonArray>()) {
    if (report["reportType"] != "net-consumption") continue;
    if (!report["createdDate"].is<uint64_t>() ||
        !report["cumulative"]["actPower"].is<double>()) return false;
    sample.stamp = report["createdDate"].as<uint64_t>();
    sample.powerW = report["cumulative"]["actPower"].as<double>();
    return sample.stamp > 0 && std::isfinite(sample.powerW);
  }
  return false;
}
