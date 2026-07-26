// scale.h
// Thin wrapper around bogde/HX711 for a 2-load-cell platform on one HX711 amp.
// Handles tare (zero) and a known-weight calibration wizard, persisting results via config.h

#pragma once
#include <HX711.h>
#include "config.h"

// ---- Pin assignment (change if you wire the HX711 to different GPIOs) ----
// Safe general-purpose pins on ESP32-C3 (avoid 2, 8, 9 which are strapping pins).
#ifndef HX711_DOUT_PIN
#define HX711_DOUT_PIN 4
#endif
#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN 5
#endif

inline HX711 hx711scale;

inline void scaleBegin() {
  hx711scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  // Apply persisted calibration
  hx711scale.set_scale(cfg.cal_factor == 0 ? 1.0 : cfg.cal_factor);
  hx711scale.set_offset(cfg.cal_offset);
}

inline bool scaleReady() {
  return hx711scale.is_ready();
}

// Current weight in configured units, using stored scale factor + offset.
inline float readWeight(uint8_t samples = 5) {
  if (!hx711scale.is_ready()) return NAN;
  return hx711scale.get_units(samples);
}

inline long readRaw(uint8_t samples = 5) {
  if (!hx711scale.is_ready()) return 0;
  return hx711scale.get_value(samples); // raw counts, offset already subtracted
}

// Zero the scale right now. persist=true also writes the new offset to config.json
// so the zero point survives a reboot; persist=false is a "soft" tare for the session only.
inline void doTare(bool persist, uint8_t samples = 20) {
  hx711scale.tare(samples);
  if (persist) {
    cfg.cal_offset = hx711scale.get_offset();
    saveConfig();
  }
}

// Calibration wizard step 2: with a known weight resting on the scale (and after
// doTare() was already called with nothing on the scale), compute and persist
// the scale factor = raw_counts / known_weight.
inline bool doCalibrate(float knownWeightUnits, uint8_t samples = 20) {
  if (knownWeightUnits <= 0) return false;

  // Read raw counts (offset already applied via current offset setting)
  hx711scale.set_scale(1.0); // temporarily read raw units
  long raw = hx711scale.get_value(samples);

  float factor = (float)raw / knownWeightUnits;
  if (factor == 0) return false;

  cfg.cal_factor = factor;
  hx711scale.set_scale(cfg.cal_factor);
  saveConfig();
  return true;
}
