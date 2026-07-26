// config.h
// Human-readable JSON configuration stored on LittleFS at /config.json
// Holds Wi-Fi credentials, AP fallback credentials, hostname, and calibration data.

#pragma once
#include <LittleFS.h>
#include <ArduinoJson.h>

#define CONFIG_PATH "/config.json"

struct ScaleConfig {
  // Station (home) Wi-Fi
  String wifi_ssid   = "";
  String wifi_pass   = "";

  // Access point (configuration) Wi-Fi - always available as a fallback
  String ap_ssid     = "ScaleSetup";
  String ap_pass     = "12345678";     // min 8 chars, or "" for open AP

  // Network identity once connected to home Wi-Fi
  String hostname     = "scale1";      // reachable at http://scale1.local

  // Calibration
  float  cal_factor  = 1.0;            // raw counts per unit (set by calibration wizard)
  long   cal_offset  = 0;              // raw zero-point offset (set by tare)

  // Display / UI
  String unit         = "kg";
  float  max_weight   = 4000.0;        // full-scale value for the dial gauge
  uint8_t decimals    = 1;             // decimal places shown

  // Boot behaviour
  uint32_t sta_timeout_ms = 60000;     // how long to try connecting before falling back to AP-only
};

inline ScaleConfig cfg;

// Templated so this works with JsonDocument, JsonObject, or JsonVariant alike
// (all three support operator[] for both reading and writing in ArduinoJson v7).

// Fill a JSON container from cfg
template <typename T>
inline void configToJson(T &doc) {
  doc["wifi_ssid"]      = cfg.wifi_ssid;
  doc["wifi_pass"]      = cfg.wifi_pass;
  doc["ap_ssid"]        = cfg.ap_ssid;
  doc["ap_pass"]        = cfg.ap_pass;
  doc["hostname"]       = cfg.hostname;
  doc["cal_factor"]     = cfg.cal_factor;
  doc["cal_offset"]     = cfg.cal_offset;
  doc["unit"]           = cfg.unit;
  doc["max_weight"]     = cfg.max_weight;
  doc["decimals"]       = cfg.decimals;
  doc["sta_timeout_ms"] = cfg.sta_timeout_ms;
}

// Populate cfg from a JSON container (missing keys keep current/default value)
template <typename T>
inline void jsonToConfig(T &doc) {
  cfg.wifi_ssid      = doc["wifi_ssid"]      | cfg.wifi_ssid;
  cfg.wifi_pass      = doc["wifi_pass"]      | cfg.wifi_pass;
  cfg.ap_ssid        = doc["ap_ssid"]        | cfg.ap_ssid;
  cfg.ap_pass        = doc["ap_pass"]        | cfg.ap_pass;
  cfg.hostname       = doc["hostname"]       | cfg.hostname;
  cfg.cal_factor     = doc["cal_factor"]     | cfg.cal_factor;
  cfg.cal_offset     = doc["cal_offset"]     | cfg.cal_offset;
  cfg.unit           = doc["unit"]           | cfg.unit;
  cfg.max_weight     = doc["max_weight"]     | cfg.max_weight;
  cfg.decimals       = doc["decimals"]       | cfg.decimals;
  cfg.sta_timeout_ms = doc["sta_timeout_ms"] | cfg.sta_timeout_ms;
}

inline bool saveConfig() {
  JsonDocument doc;
  configToJson(doc);

  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) {
    Serial.println("[config] failed to open config.json for writing");
    return false;
  }
  // Pretty-printed so the file stays human readable/editable on the flash filesystem.
  serializeJsonPretty(doc, f);
  f.close();
  Serial.println("[config] saved");
  return true;
}

inline bool loadConfig() {
  if (!LittleFS.exists(CONFIG_PATH)) {
    Serial.println("[config] no config.json found, writing defaults");
    return saveConfig();
  }

  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    Serial.println("[config] failed to open config.json for reading");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print("[config] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  jsonToConfig(doc);
  Serial.println("[config] loaded");
  return true;
}

inline bool initFilesystem() {
  // 'true' formats LittleFS automatically on first run / if mount fails.
  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS mount failed");
    return false;
  }
  return true;
}
