# ESP32-C3 Networked Scale

A Wi-Fi scale for two 2-ton load cells on a single HX711, with a browser dashboard
(text + circular dial), a setup/calibration page, and config stored as a
human-readable JSON file on LittleFS.

## 1. Arduino IDE setup

**Board support (Boards Manager URL):**
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Add this in File -> Preferences -> "Additional Board Manager URLs", then in
Tools -> Board -> Boards Manager install **esp32 by Espressif Systems** (v3.x).

**Board selection:** Tools -> Board -> ESP32 Arduino -> **ESP32C3 Dev Module**.
For "Super Mini"/generic ESP32-C3 boards also set:
- USB CDC On Boot: **Enabled** (so Serial shows up over the USB port)
- Flash Size: 4MB (adjust if yours differs)
- Partition Scheme: Default (or "Default w/ SPIFFS" — either works, LittleFS
  is created on the SPIFFS/data partition automatically at first boot)

**Libraries (Sketch -> Include Library -> Manage Libraries):**
| Library | Author | Notes |
|---|---|---|
| HX711 | bogde | Load cell amplifier driver |
| ArduinoJson | Benoit Blanchon | v7.x |
| ESPAsyncWebServer | ESP32Async | Use the **ESP32Async** org's fork, not the old me-no-dev one — it's the maintained one that supports current ESP32 core versions |
| AsyncTCP | ESP32Async | Dependency of the above, ESP32Async fork |

`LittleFS` and `ESPmDNS` ship with the esp32 board package, no separate install needed.

## 2. Wiring

| Signal | ESP32-C3 GPIO | Notes |
|---|---|---|
| HX711 DOUT | GPIO4 | change via `HX711_DOUT_PIN` in `scale.h` |
| HX711 SCK  | GPIO5 | change via `HX711_SCK_PIN` in `scale.h` |
| HX711 VCC  | 3V3 | |
| HX711 GND  | GND | |
| Config button | GPIO9 (onboard BOOT button) | see caveat below |
| Status LED (optional) | GPIO8 | many "Super Mini" boards have one built in here |

**Two load cells on one HX711:** wire both cells' E+/E- (excitation) together
and both S+/S- (signal) together into the HX711's A+/A- inputs — this is the
standard "2-cell summing" arrangement used on small platform scales. For best
accuracy use a proper summing/junction board matched to your cells rather than
bare parallel wiring, since small cell-to-cell imbalances otherwise show up as
corner-loading error.

**About GPIO9 as the config button:** it's the onboard BOOT button on most
ESP32-C3 boards, and it's a strapping pin used to enter the UART bootloader
when held low across an EN/RESET pulse. Reading it in software after boot
(what this sketch does) is completely normal and is how most ESP32-C3 dev
board "boot buttons" get reused as general inputs. The only thing to avoid is
holding it down while power-cycling via the EN pin if you want to be sure you
land in the sketch rather than the bootloader. If that bothers you, wire an
external button to a plain GPIO (e.g. GPIO6) and change `BUTTON_PIN` in
`ScaleServer.ino`.

## 3. Sketch layout

```
ScaleServer/
  ScaleServer.ino   main sketch: Wi-Fi state machine, HTTP + WebSocket routes
  config.h          Config struct, load/save as pretty-printed JSON on LittleFS
  scale.h           HX711 wrapper: read, tare, calibrate
  pages.h           Dashboard + setup page HTML/CSS/JS (served from flash, PROGMEM)
```
Open the folder in Arduino IDE — the extra `.h` files show up as tabs
alongside `ScaleServer.ino`. No separate "upload LittleFS data" step is
needed; only `/config.json` lives on LittleFS and the sketch creates it
itself on first boot with sensible defaults.

## 4. First boot / normal use

1. Flash the sketch. On first boot there's no saved Wi-Fi, so it comes up in
   AP-only mode: SSID `ScaleSetup`, password `12345678` (both changeable),
   at `192.168.4.1`.
2. Join that network from your laptop/phone and open `http://192.168.4.1/setup`.
3. Enter your home Wi-Fi SSID/password (use **Scan** to pick from a list),
   set a hostname (e.g. `scale1`), adjust AP password if you like, then
   **Save & Reboot**.
4. From then on, at every boot:
   - **Hold the config button (BOOT/GPIO9) while powering on** → device stays
     AP-only, skips joining home Wi-Fi. Use this if you need to get back into
     setup and don't remember/trust the home network.
   - **Don't hold it** → device tries to join the stored home Wi-Fi for up to
     `sta_timeout_ms` (default 60s). On success it's reachable at
     `http://<hostname>.local` (and its DHCP-assigned IP). If it can't
     connect within the timeout, it simply stays on AP-only — you're never
     locked out.
   - The AP is **always on** in both cases, so `http://192.168.4.1/` and
     `/setup` are always reachable as a safety net regardless of Station
     status.
5. The dashboard (`/`) shows the live weight as text and on a circular dial,
   pushed over a WebSocket a few times a second, with a **Tare** button.

## 5. Calibration

On the `/setup` page, "Calibration" section:
1. Remove all weight from the scale, click **Tare / Zero (saved)**. This
   zeroes the HX711 offset and writes it to `config.json`.
2. Place a known reference weight on the scale, enter its value in the same
   units you want displayed (matches the "Unit label" field), click
   **Calibrate**. This computes `raw_counts / known_weight` and saves it as
   the scale factor.
3. Recheck the raw/weight readout shown live on that page. Re-tare/re-calibrate
   any time — both steps persist immediately, no reboot required.

## 6. API reference (used internally by the web pages, handy for scripting too)

| Method | Path | Body | Response |
|---|---|---|---|
| GET | `/api/weight` | — | `{ "weight": 12.3, "raw": 123456 }` |
| POST | `/api/tare` | — | `{ "ok": true }` — zeroes and persists offset |
| GET | `/api/config` | — | full config JSON (passwords masked) |
| POST | `/api/config` | config JSON (blank password = keep existing) | `{ "ok": true }` |
| POST | `/api/calibrate` | `{ "known_weight": 20.0 }` | `{ "ok": true, "factor": 1234.5 }` |
| GET | `/api/scan` | — | `[{ "ssid": "...", "rssi": -55 }, ...]` |
| POST | `/api/reboot` | — | `{ "ok": true }`, then restarts |
| WS | `/ws` | — | pushes `{ weight, raw, max_weight, unit, decimals }` periodically |

## 7. config.json (on LittleFS, human-editable)

```json
{
  "wifi_ssid": "MyHomeNetwork",
  "wifi_pass": "••••••••",
  "ap_ssid": "ScaleSetup",
  "ap_pass": "12345678",
  "hostname": "scale1",
  "cal_factor": 1234.56,
  "cal_offset": 8421,
  "unit": "kg",
  "max_weight": 4000.0,
  "decimals": 1,
  "sta_timeout_ms": 60000
}
```
You normally never need to touch this file directly — the setup page and
calibration wizard write it for you — but it's plain JSON if you ever want to
inspect or hand-edit it (e.g. via a LittleFS filesystem browser sketch).
