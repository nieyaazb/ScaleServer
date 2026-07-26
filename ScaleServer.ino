// ScaleServer.ino
// ESP32-C3 networked platform scale
//   - 2x load cells -> single HX711 -> weight readout
//   - Config + calibration stored as human-readable JSON on LittleFS (/config.json)
//   - Always-on Access Point for configuration; optionally joins home Wi-Fi (Station)
//   - Boot-time button: hold to force AP-only mode; otherwise tries Station connect
//     for up to cfg.sta_timeout_ms (default 60s)
//   - Web dashboard: live weight as text + circular dial (SVG gauge) over WebSocket
//   - Reachable via configurable mDNS hostname, e.g. http://scale1.local
//
// Required libraries (Arduino Library Manager):
//   - HX711 by bogde
//   - ArduinoJson by Benoit Blanchon (v7)
//   - ESPAsyncWebServer by ESP32Async
//   - AsyncTCP by ESP32Async
// Board: "ESP32C3 Dev Module" (esp32 core by Espressif Systems, v3.x)
// See README.md for wiring, board manager URL, and full setup steps.

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "config.h"
#include "scale.h"
#include "pages.h"

// ---- Boot-mode button ---------------------------------------------------
// Default: reuse the onboard BOOT button (GPIO9) so no extra wiring is needed.
// NOTE: GPIO9 is a strapping pin used to enter UART download mode when held
// low across an EN/RESET pulse. Reading it in software after boot (as done
// below) is safe and standard practice; just don't hold it through a manual
// EN reset if you want to avoid accidentally entering the bootloader.
// Wire an external momentary switch to a different GPIO instead if you'd
// rather not use GPIO9 for this.
#ifndef BUTTON_PIN
#define BUTTON_PIN 9
#endif

// Onboard status LED, if your board has one wired to GPIO8 (many "Super Mini"
// boards do). Comment out the blink calls below if you don't have one.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 8
#endif

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

bool staConnected = false;
unsigned long lastBroadcast = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long BROADCAST_INTERVAL_MS = 250;
const unsigned long RECONNECT_RETRY_MS = 30000;

// ---------------------------------------------------------------------------
// Wi-Fi bring-up
// ---------------------------------------------------------------------------
void startAP() {
  WiFi.mode(WIFI_AP_STA);
  bool ok;
  if (cfg.ap_pass.length() >= 8) {
    ok = WiFi.softAP(cfg.ap_ssid.c_str(), cfg.ap_pass.c_str());
  } else {
    ok = WiFi.softAP(cfg.ap_ssid.c_str()); // open network if no valid password set
  }
  Serial.printf("[wifi] AP '%s' %s, IP: %s\n",
                cfg.ap_ssid.c_str(), ok ? "started" : "FAILED",
                WiFi.softAPIP().toString().c_str());
}

// Blocking attempt to join home Wi-Fi, up to timeoutMs. Returns true on success.
bool connectSTA(unsigned long timeoutMs) {
  if (cfg.wifi_ssid.length() == 0) {
    Serial.println("[wifi] no station SSID configured, skipping");
    return false;
  }

  Serial.printf("[wifi] connecting to '%s' (timeout %lums)...\n", cfg.wifi_ssid.c_str(), timeoutMs);
  WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("[wifi] station connect timed out, staying on AP only");
  return false;
}

void startMDNS() {
  String host = cfg.hostname.length() ? cfg.hostname : "scale1";
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mdns] available at http://%s.local\n", host.c_str());
  } else {
    Serial.println("[mdns] failed to start");
  }
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[ws] client #%u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[ws] client #%u disconnected\n", client->id());
  }
}

void broadcastWeight() {
  if (ws.count() == 0) return;
  JsonDocument doc;
  doc["weight"]     = readWeight(3);
  doc["raw"]        = readRaw(3);
  doc["max_weight"] = cfg.max_weight;
  doc["unit"]       = cfg.unit;
  doc["decimals"]   = cfg.decimals;
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

// ---------------------------------------------------------------------------
// HTTP routes
// ---------------------------------------------------------------------------
void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", SETUP_HTML);
  });

  // Current live reading
  server.on("/api/weight", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["weight"] = readWeight(3);
    doc["raw"]    = readRaw(3);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Zero the scale (always persisted, per the physical Tare button's role on the dashboard)
  server.on("/api/tare", HTTP_POST, [](AsyncWebServerRequest *req) {
    doTare(true);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // Get current config. Secrets are masked (blank) unless the setup page just set them.
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    configToJson(doc);
    doc["wifi_pass"] = "";     // never echo secrets back to the browser
    doc["ap_pass"]   = "";
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Update config. Blank wifi_pass/ap_pass in the payload means "keep existing password".
  AsyncCallbackJsonWebHandler *configHandler = new AsyncCallbackJsonWebHandler(
    "/api/config",
    [](AsyncWebServerRequest *req, JsonVariant &json) {
      String newWifiPass = json["wifi_pass"] | "";
      String newApPass   = json["ap_pass"]   | "";
      String oldWifiPass = cfg.wifi_pass;
      String oldApPass   = cfg.ap_pass;

      jsonToConfig(json); // fills everything, including blank passwords

      // Blank password in the request means "keep what's already stored"
      if (newWifiPass.length() == 0) cfg.wifi_pass = oldWifiPass;
      if (newApPass.length() == 0)   cfg.ap_pass   = oldApPass;

      saveConfig();
      req->send(200, "application/json", "{\"ok\":true}");
    });
  server.addHandler(configHandler);

  // Calibration wizard step 2
  AsyncCallbackJsonWebHandler *calHandler = new AsyncCallbackJsonWebHandler(
    "/api/calibrate",
    [](AsyncWebServerRequest *req, JsonVariant &json) {
      float known = json["known_weight"] | 0.0;
      bool ok = doCalibrate(known);
      JsonDocument resp;
      resp["ok"] = ok;
      resp["factor"] = cfg.cal_factor;
      String out;
      serializeJson(resp, out);
      req->send(200, "application/json", out);
    });
  server.addHandler(calHandler);

  // Wi-Fi network scan for the setup page's SSID picker.
  // Blocking (~2-4s) but simple/reliable; only runs when the user taps "Scan".
  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
    WiFi.scanDelete();
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "Not found");
  });
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[boot] ESP32-C3 networked scale starting");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
#ifdef STATUS_LED_PIN
  pinMode(STATUS_LED_PIN, OUTPUT);
#endif

  initFilesystem();
  loadConfig();
  scaleBegin();

  bool buttonHeld = (digitalRead(BUTTON_PIN) == LOW);
  Serial.printf("[boot] config button %s\n", buttonHeld ? "HELD -> forcing AP-only mode" : "not held");

  startAP(); // AP is always available for configuration access

  if (!buttonHeld) {
    staConnected = connectSTA(cfg.sta_timeout_ms);
  } else {
    Serial.println("[wifi] skipping station connect (button held at boot)");
  }

  if (staConnected) startMDNS();

  setupRoutes();
  server.begin();
  Serial.println("[http] server started on port 80");
  Serial.printf("[http] AP:      http://%s\n", WiFi.softAPIP().toString().c_str());
  if (staConnected) {
    Serial.printf("[http] Station: http://%s  or  http://%s.local\n",
                  WiFi.localIP().toString().c_str(), cfg.hostname.c_str());
  }
}

void loop() {
  ws.cleanupClients();

  unsigned long now = millis();
  if (now - lastBroadcast >= BROADCAST_INTERVAL_MS) {
    lastBroadcast = now;
    broadcastWeight();
  }

  // Non-blocking periodic retry if station Wi-Fi drops or was never connected
  if (cfg.wifi_ssid.length() > 0 && WiFi.status() != WL_CONNECTED) {
    if (now - lastReconnectAttempt >= RECONNECT_RETRY_MS) {
      lastReconnectAttempt = now;
      Serial.println("[wifi] attempting background reconnect...");
      WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
    }
  } else if (WiFi.status() == WL_CONNECTED && !staConnected) {
    staConnected = true;
    startMDNS();
    Serial.printf("[wifi] station (re)connected, IP: %s\n", WiFi.localIP().toString().c_str());
  }
}
