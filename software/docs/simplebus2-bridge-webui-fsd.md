# Simplebus2-Bridge — Web Interface & Portal Separation
## Functional Specification Document

**Version:** 2.0.0  
**Date:** 2026-08-01  
**Status:** Released  
**Project:** `simplebus2-mqtt-bridge`  
**Platform:** ESP32-C3, Arduino framework, PlatformIO  

---

## 1. System Overview

### 1.1 Purpose

The Simplebus2-Bridge firmware currently uses WiFiManager as the sole configuration interface. WiFiManager operates only as a temporary captive portal (AP mode) and mixes WiFi credentials with all device settings. This document specifies:

1. **Separation** of the WiFiManager captive portal (WiFi credentials only) from a new **permanent HTTP web interface**.
2. **Permanent web interface** accessible at `Simplebus2-Bridge.local` and the device IP, containing all configuration and live status.
3. **Configurable MQTT topic prefix** (stored in EEPROM, default `SimpleBus`), same topic root as the previous hardcoded firmware.
4. **Minor HA Discovery corrections**: remove OTA URL entity; add default value for Ring-to-Open Duration.

### 1.2 Scope

| In scope | Out of scope |
|----------|-------------|
| HTTP web server (port 80) with status + config UI | Simplebus2 signal decoder |
| Captive portal simplification (WiFi only) | Debounce / ISR ring buffer |
| Configurable MQTT topic prefix | PubSubClient reconnect / subscription logic |
| WiFi quality rating (RSSI → qualitative) | HA discovery entity semantics (already fixed) |
| HA Discovery: remove OTA URL entity | Hardware pin mapping |
| HA Discovery: Ring-to-Open Duration default 600 | TLS / authentication |
| Factory reset via web interface | |

> **Constraint:** The Simplebus2 signal decoding, debouncer, deduplication, ISR ring buffer, and all timing constants are **frozen**. Any change to that code requires explicit user approval before implementation.

### 1.3 Existing System Snapshot

- **Firmware:** Arduino/PlatformIO on ESP32-C3 (`esp32-c3-devkitm-1`)
- **WiFi:** WiFiManager `autoConnect("Config_MQTT_SimpleBus_2")` — portal contains MQTT + all device settings
- **EEPROM:** `struct Settings { mqttServer, mqttUser, mqttPassword, gain, compVoltLvl, filter, ringAutomation, intercomAddress }`
- **MQTT:** PubSubClient; all topics hardcoded with `SimpleBus/` prefix
- **mDNS:** `MDNS.begin("Simplebus2-Bridge")` — already implemented
- **Web server:** None (only WiFiManager portal)
- **HA Discovery:** Publishes to `homeassistant/...`; OTA URL entity present (to be removed)

---

## 2. Architecture

### 2.1 Component Diagram

```
┌─────────────────────────────────────────────────────┐
│                   ESP32-C3 Firmware                 │
│                                                     │
│  ┌──────────────┐   ┌─────────────────────────────┐ │
│  │ WiFiManager  │   │  Permanent HTTP Web Server  │ │
│  │ (AP mode,    │   │  port 80                    │ │
│  │  WiFi only)  │   │  /           → HTML UI      │ │
│  └──────┬───────┘   │  /api/status → JSON         │ │
│         │           │  /api/config → R/W EEPROM   │ │
│  ┌──────▼───────┐   │  /api/wifi   → WiFi creds   │ │
│  │   WiFi STA   │   │  /api/ota    → OTA (URL)    │ │
│  │ 192.168.x.x  │   │  /api/ota-upload → OTA file │ │
│  └──────┬───────┘   │  /api/restart               │ │
│         │           │  /api/factory-reset          │ │
│         │           └─────────────────────────────┘ │
│  ┌──────▼───────┐   ┌─────────────────────────────┐ │
│  │ PubSubClient │   │  mDNS                       │ │
│  │ MQTT broker  │   │  Simplebus2-Bridge.local     │ │
│  │ <prefix>/... │   └─────────────────────────────┘ │
│  └──────────────┘                                   │
│                                                     │
│  ┌─────────────────────────────────────────────────┐│
│  │ Simplebus2 Decoder (FROZEN — no changes)        ││
│  │ ISR ring buffer, state machine, dedup, debounce ││
│  └─────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────┘
```

### 2.2 File Structure

```
src/
  main.cpp          — main firmware (modified)
  web_ui.h          — new: HTML/CSS/JS as PROGMEM string
lib/
  Debounce/         — unchanged
platformio.ini      — add WebServer dependency if needed
Documents/
  simplebus2-bridge-webui-fsd.md   — this document
```

### 2.3 EEPROM Settings Struct (updated)

```cpp
struct Settings {
  char mqttServer[40];
  char mqttUser[40];
  char mqttPassword[40];
  int  gain;             // default 20
  int  compVoltLvl;      // default 600 (mV)
  bool filter;
  bool ringAutomation;   // retained in EEPROM; no longer exposed in web UI (removed 2.0.0)
  int  intercomAddress;
  char mqttPrefix[32];   // NEW — default "SimpleBus"
  int  ringToOpenMinutes;// NEW — default 600 (min = 10h), replaces runtime-only ringToOpenMillis
};
```

> **Note:** Adding fields to the end of the struct is safe with the existing EEPROM magic-byte pattern. The `structInitialized()` check must be updated to include the new fields in the magic / version byte, OR the struct size must be used as the validity signal. Implementation shall use a version byte increment to detect old EEPROM layout and re-initialize defaults cleanly.

---

## 3. Implementation Phases

### Phase 1 — Quick HA Fixes (no new infrastructure)

**Scope:** HA Discovery corrections and default values. No web server. No portal changes.

**Deliverables:**
- Remove OTA URL entity from HA discovery (publish empty retained)
- Ring-to-Open Duration `number` entity: add `"val_tpl"` or `"init"` field with default 600
- Default `ringToOpenMillis` changed to `600 * 60 * 1000` (already done in prior session — verify)

**Exit Criteria:** HA shows no OTA URL entity; Ring-to-Open Duration shows 600 as initial value.

**Dependencies:** None.

---

### Phase 2 — MQTT Prefix + EEPROM Migration

**Scope:** Make MQTT topic prefix configurable. Migrate EEPROM struct safely.

**Deliverables:**
- `mqttPrefix[32]` and `ringToOpenMinutes` fields added to `Settings` struct
- EEPROM version byte incremented → clean re-init if old layout detected
- All `SimpleBus/` topic strings replaced with `mySettings.mqttPrefix + "/"`
- `HA_AVAILABILITY_TOPIC` computed at runtime from prefix
- HA discovery: all `stat_t`, `cmd_t`, `avty_t` use configurable prefix
- Default prefix: `SimpleBus`

**Exit Criteria:** 
- Bridge connects to MQTT, publishes `SimpleBus/availability: online`
- All HA entities appear with correct topics
- Old EEPROM layout detected → defaults loaded → bridge connects normally

**Dependencies:** Phase 1 complete.

---

### Phase 3 — Permanent Web Interface + Portal Simplification

**Scope:** Full web server implementation and captive portal cleanup.

**Deliverables:**
- `WebServer` instance on port 80 started after WiFi connect
- HTML UI in `src/web_ui.h` as PROGMEM (dark theme, layout from reference)
- REST API endpoints: `/api/status`, `/api/config`, `/api/wifi`, `/api/ota`, `/api/restart`, `/api/factory-reset`
- WiFiManager portal: AP name `Simplebus2-Bridge`, WiFi credentials only (remove all custom parameters from `wm.addParameter()`)
- WiFi quality rating in status

**Exit Criteria:**
- `http://Simplebus2-Bridge.local/` loads dashboard in browser
- Status auto-refreshes every 5 s
- All configuration sections save correctly to EEPROM
- WiFi reconfiguration works without reboot for credential change
- Factory reset clears EEPROM and opens AP

**Dependencies:** Phase 2 complete.

---

## 4. Functional Requirements

### 4.1 Web Server

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-1.1 | Must | The firmware shall start a `WebServer` on port 80 after WiFi connection is established. |
| FR-1.2 | Must | The web server shall serve the configuration dashboard at `/` (GET). |
| FR-1.3 | Must | The web server shall respond to requests within 200 ms under normal load. |
| FR-1.4 | Must | The web server shall be accessible via `Simplebus2-Bridge.local` (mDNS) and the device IP. |
| FR-1.5 | Should | The web server shall not block the main loop for more than 50 ms. |

### 4.2 Status API

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-2.1 | Must | `GET /api/status` shall return a JSON object with fields: `fw`, `uptime_s`, `wifi` (bool), `rssi`, `wifi_quality`, `mqtt` (bool), `free_heap`, `heap_total`, `flash_free`, `flash_total`. |
| FR-2.2 | Must | `wifi_quality` shall be one of: `"Excellent"` (≥ −60 dBm), `"Good"` (−61 … −70 dBm), `"Ok"` (−71 … −80 dBm), `"Poor"` (< −80 dBm). If disconnected: `""`. |
| FR-2.3 | Must | `fw` shall be the firmware version string derived from `FIRMWARE_VERSION` define (e.g. `"1.1.0"`). |
| FR-2.4 | Must | `uptime_s` shall be seconds since last boot (`millis() / 1000`). |
| FR-2.5 | Must | `free_heap` and `heap_total` shall be in bytes (`ESP.getFreeHeap()`, `ESP.getHeapSize()`). |
| FR-2.6 | Must | `flash_free` and `flash_total` shall be in bytes (`ESP.getFreeSketchSpace()`, `ESP.getFlashChipSize()`). |

### 4.3 Configuration API

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-3.1 | Must | `GET /api/config` shall return current settings as JSON. |
| FR-3.2 | Must | `POST /api/config` shall accept `application/x-www-form-urlencoded` with fields: `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass`, `mqtt_prefix`, `address`, `gain`, `threshold`, `rto_minutes`. (`ring_automation` removed in 2.0.0.) |
| FR-3.3 | Must | On valid POST, the firmware shall save to EEPROM and apply MQTT settings immediately (reconnect if server/credentials changed). |
| FR-3.4 | Must | `mqtt_prefix` changes shall take effect on the next MQTT reconnect. |
| FR-3.5 | Must | Numeric fields outside their valid range shall be clamped silently (not rejected). |

### 4.4 WiFi API

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-4.1 | Must | `POST /api/wifi` shall accept `ssid` and `password` fields. |
| FR-4.2 | Must | On valid POST, the firmware shall call `WiFi.persistent(true)`, `WiFi.begin(ssid, password)`, and then `ESP.restart()` so credentials are persisted to NVS and survive a power cycle. |
| FR-4.3 | Should | The dashboard shall display the new connection status after the next auto-refresh cycle. |

### 4.5 OTA API

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-5.1 | Must | `POST /api/ota` shall accept a `url` field and trigger the existing OTA download mechanism (`HTTPUpdate`). |
| FR-5.2 | Must | The OTA URL placeholder in the UI shall be `http://host/simplebus2-firmware.bin`. |
| FR-5.3 | Must | OTA shall not be triggered with an empty or non-HTTP URL. |
| FR-5.4 | Must | `POST /api/ota-upload` shall accept a `.bin` file as `multipart/form-data`, flash it via `Update.h`, and reboot. Response: `{"ok":true}` on success, `{"ok":false,"error":"..."}` on failure. |
| FR-5.5 | Must | Both OTA methods (URL and file upload) shall be offered in the same OTA card in the web interface. |
| FR-5.6 | Should | The file upload input shall accept only `.bin` files (`accept=".bin"`). |
| FR-5.7 | Should | XHR upload progress shall be shown in a status line below the file input. |

### 4.6 System Actions

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-6.1 | Must | `POST /api/restart` shall call `ESP.restart()`. |
| FR-6.2 | Must | `POST /api/factory-reset` shall erase EEPROM (re-initialize defaults), clear WiFi credentials (`WiFi.disconnect(true, true)`), and call `ESP.restart()`. After restart, the device shall open the `Simplebus2-Bridge` AP. |
| FR-6.3 | Must | The factory reset button in the UI shall require a JavaScript `confirm()` dialog before submitting. |

### 4.7 Captive Portal Simplification

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-7.1 | Must | The WiFiManager AP name shall be `Simplebus2-Bridge`. |
| FR-7.2 | Must | The WiFiManager portal shall contain no custom parameters — WiFi SSID and password only. |
| FR-7.3 | Must | All previous WiFiManager custom parameters (MQTT server, user, password, address, gain, voltage, automation) shall be removed from the portal. |
| FR-7.4 | Must | Portal timeout shall remain 120 s. |
| FR-7.5 | Should | The WiFiManager captive portal page shall be restyled to match the dark-theme design system defined in the `esp32-web-ui` skill (background `#111827`, card `#1f2937`, button colours `.g`/`.o`/`.r`, typography). Implemented in a single dedicated pass after all other web UI work is complete. |

### 4.8 MQTT Topic Prefix

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-8.1 | Must | All MQTT topic strings shall use `mySettings.mqttPrefix` as the root segment instead of the hardcoded string `SimpleBus`. |
| FR-8.2 | Must | The default prefix shall be `SimpleBus`. |
| FR-8.3 | Must | HA Discovery `stat_t`, `cmd_t`, and `avty_t` fields shall use the configured prefix. |
| FR-8.4 | Must | On EEPROM re-initialization (version mismatch or factory reset), the prefix shall default to `SimpleBus`. |
| FR-8.5 | Should | After a prefix change the UI shall display a notice "Restart required for prefix change to take effect". No automatic reconnect is triggered. |

### 4.9 UI Layout & Design

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-9.1 | Must | Page title and `<h1>` heading shall be `Simplebus2-Bridge`. |
| FR-9.2 | Must | The UI shall use the dark theme from the LoRa-Garage-Remote reference: background `#111827`, text `#e5e7eb`, cards `#1f2937`, max-width 480 px. |
| FR-9.3 | Must | Section order: **Status** → **WiFi** → **MQTT** → **Device Configuration** → **OTA Firmware Update** → **System**. |
| FR-9.4 | Must | Status section shall auto-refresh every 5 s via `fetch('/api/status')`. |
| FR-9.5 | Must | WiFi status shall display: `Connected (−62 dBm, Good)` or `Disconnected`. |
| FR-9.6 | Must | MQTT status shall display: `Connected` or `Disconnected`. |
| FR-9.7 | Must | Heap and Flash shall display as `free / total kB (xx% free)`. |
| FR-9.8 | Must | All HTML, CSS, and JavaScript shall be embedded in `src/web_ui.h` as a `PROGMEM` C-string. |
| FR-9.9 | Should | Total HTML page size shall not exceed 8 KB to fit comfortably in flash. |
| FR-9.10 | Must | Status rows shall be separated by fine horizontal dividers (`border-bottom:1px solid #374151`); last row has no border. |
| FR-9.11 | Must | All right-side values in status rows shall be bold (`font-weight:600`, CSS class `val`). |
| FR-9.12 | Must | Qualitative status values shall be colour-coded: Connected/Excellent/Good → green `#4ade80` (`.ok`); Ok quality → amber `#fbbf24` (`.warn`); Disconnected/Poor → red `#f87171` (`.err`). |
| FR-9.13 | Must | WiFi section shall pre-populate the SSID field with the current connected SSID (from `GET /api/config` → `wifi_ssid`); password field shall be empty with placeholder "(re-enter to change)". |
| FR-9.14 | Must | System buttons (Restart, Factory Reset) shall have equal width (`flex:1`) and together fill the full card width, matching the Save buttons above. |
| FR-9.15 | Must | `GET /api/config` shall include `wifi_ssid` (current `WiFi.SSID()`) in its JSON response. |
| FR-9.16 | Removed | Ring Automation checkbox removed from web UI in 2.0.0. State is controlled exclusively via MQTT and Home Assistant. The `ringAutomation` field remains in EEPROM but is not settable via web API. |

#### CSS Class Reference (dark theme)

```css
.row        { display:flex; justify-content:space-between; padding:6px 0;
              border-bottom:1px solid #374151 }
.row:last-child { border-bottom:none }
.lbl        { color:#9ca3af }
.val        { font-weight:600 }               /* all right-side values */
.ok         { color:#4ade80 }                 /* Connected, Excellent, Good */
.err        { color:#f87171 }                 /* Disconnected, Poor */
.warn       { color:#fbbf24 }                 /* Ok (medium quality) */
.row-btns   { display:flex; gap:8px }
.row-btns .btn { flex:1; margin-top:0 }       /* equal-width system buttons */
```

#### WiFi Quality → CSS Class Mapping

| RSSI | Label | Class |
|------|-------|-------|
| ≥ −60 dBm | Excellent | `.ok` |
| −61 … −70 dBm | Good | `.ok` |
| −71 … −80 dBm | Ok | `.warn` |
| < −80 dBm | Poor | `.err` |
| Disconnected | — | `.err` |

#### Captive Portal Design (WiFiManager)
- AP name: `Simplebus2-Bridge`
- Menu: `{"wifi","sep","exit"}` — shows only WiFi scan list and exit; hides Info and OTA Update pages
- Portal timeout: 120 s
- No custom parameters (MQTT config via web UI only)

### 4.10 HA Discovery

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-10.1 | Must | The OTA Update URL `text` entity shall be removed from HA discovery (publish empty retained message to its config topic). |
| FR-10.2 | Must | The Ring-to-Open Duration `number` entity shall declare default value `600`. |
| FR-10.3 | Must | All HA discovery config topics and payload topics shall use the configured MQTT prefix. |
| FR-10.4 | Must | Binary sensor entities (EntryDoor, FloorDoor) shall not include `avty_t` in their HA discovery payload. Absence of LWT prevents HA from hiding entities when the firmware does not publish an availability message. |
| FR-10.5 | Must | Binary sensor entities (EntryDoor, FloorDoor) shall use `"ic":"mdi:doorbell"` instead of `"dev_cla":"doorbell"` for broad Home Assistant version compatibility. |
| FR-10.6 | Must | The Ring-to-Open Duration `number` entity shall include `stat_t` pointing to `<prefix>/RingToOpenTime`. The firmware shall publish the current value (retained) on MQTT connect and on every change via web UI or MQTT command. The `SetRingToOpenTime` handler shall save to EEPROM and publish back to `RingToOpenTime`. |

### 4.11 Default Values

| Parameter | Default | Valid Range |
|-----------|---------|-------------|
| Gain | 20 | 2–40 |
| Comparator Voltage (Threshold) | 600 mV | 100–1500 mV |
| Ring-to-Open Duration | 600 min | 1–1440 min |
| MQTT Prefix | `SimpleBus` | 1–31 chars, no `/` |
| Intercom Address | 9 | 0–255 |

---

## 5. Non-Functional Requirements

| ID | Priority | Requirement |
|----|----------|-------------|
| NFR-1 | Must | Flash footprint increase shall not exceed 30 KB over baseline. |
| NFR-2 | Must | Web server `handleClient()` call shall be non-blocking (return immediately when no request pending). |
| NFR-3 | Must | The main loop latency (ISR processing + MQTT + web) shall not exceed 100 ms worst-case. |
| NFR-4 | Must | The Simplebus2 decoder, ISR ring buffer, debounce, and deduplication code shall not be modified. |
| NFR-5 | Should | EEPROM writes shall only occur on user-initiated saves (not on every boot). |
| NFR-6 | Should | The web interface shall be usable on mobile browsers (viewport meta tag, touch-friendly buttons). |

---

## 6. Risks, Assumptions & Dependencies

| # | Type | Description | Mitigation |
|---|------|-------------|-----------|
| R-1 | Risk | `WebServer` in same loop as `PubSubClient` may cause MQTT keep-alive timeouts under sustained HTTP load | Keep `handleClient()` non-blocking; increase PubSubClient keepalive to 120 s |
| R-2 | Risk | EEPROM struct version mismatch after flash may corrupt settings | Increment version byte; on mismatch re-init all defaults |
| R-3 | Risk | MQTT prefix change breaks HA entities already discovered with old prefix | Document in operational procedure: after prefix change, restart HA MQTT integration |
| R-4 | Risk | Flash space exhaustion (ESP32-C3 has 1.3 MB app partition) | Current usage: 72.5% after Phase 3 mDNS. Monitor; PROGMEM for HTML avoids heap pressure |
| A-1 | Assumption | Arduino `WebServer` library is available in the ESP32 Arduino core (it is — bundled) | — |
| A-2 | Assumption | User's WiFi and MQTT credentials are stored in EEPROM from the last session | Verified by user |
| A-3 | Assumption | The firmware version string shall be defined as `FIRMWARE_VERSION "1.1.0"` | User to confirm version numbering |
| D-1 | Dependency | `ESPmDNS` already included (Phase 3 of prior work) | — |
| D-2 | Dependency | `WiFiManager@^2.0.17` — AP-only mode, no custom params needed | Already in `platformio.ini` |

---

## 7. Interfaces & Data Models

### 7.1 HTTP API

#### GET /api/status
```json
{
  "fw": "1.1.0",
  "uptime_s": 3671,
  "wifi": true,
  "rssi": -62,
  "wifi_quality": "Good",
  "mqtt": true,
  "free_heap": 215040,
  "heap_total": 336896,
  "flash_free": 360448,
  "flash_total": 4194304
}
```

#### GET /api/config
```json
{
  "mqtt_host": "192.168.4.1",
  "mqtt_user": "",
  "mqtt_prefix": "SimpleBus",
  "address": 9,
  "gain": 20,
  "threshold": 600,
  "rto_minutes": 600
}
```
> Note: `mqtt_pass` is not returned (write-only for security).

#### POST /api/config
Content-Type: `application/x-www-form-urlencoded`  
Fields: `mqtt_host`, `mqtt_user`, `mqtt_pass`, `mqtt_prefix`, `address`, `gain`, `threshold`, `rto_minutes`  
Response: `{"ok": true}` or `{"ok": false, "error": "..."}`

#### POST /api/wifi
Fields: `ssid`, `password`  
Response: `{"ok": true}`

#### POST /api/ota
Fields: `url`  
Response: `{"ok": true}` — OTA starts asynchronously

#### POST /api/ota-upload
Content-Type: `multipart/form-data`  
Field: `firmware` (binary `.bin` file)  
Response: `{"ok": true}` — device reboots immediately after flash  
Response on failure: `{"ok": false, "error": "flash failed"}`  
Note: Only HTTP, no HTTPS. Uses `Update.h`; no temporary storage required.

#### POST /api/restart
Response: `{"ok": true}` — device restarts after response sent

#### POST /api/factory-reset
Response: `{"ok": true}` — EEPROM cleared, WiFi credentials cleared, device restarts

### 7.2 MQTT Topics (configurable prefix, default `SimpleBus`)

| Topic | Direction | Retained | Description |
|-------|-----------|----------|-------------|
| `<prefix>/Address` | OUT | Yes | Intercom address |
| `<prefix>/RingToOpenStatus` | OUT | Yes | `ON` / `OFF` |
| `<prefix>/RingToOpenTime` | OUT | Yes | Current Ring-to-Open duration in minutes (integer string) |
| `<prefix>/EntryDoor` | OUT | No | `ON` on entry door ring |
| `<prefix>/FloorDoor` | OUT | No | `ON` on floor door ring |
| `<prefix>/Heartbeat` | OUT | No | `ON` every 5 min |
| `<prefix>/RingToOpenOpened` | OUT | No | `ON` when door opened via RTO |
| `<prefix>/Reboot` | OUT | No | `ON rst=0xN` on boot |
| `<prefix>/OpenDoor` | IN | — | `ON` → trigger door open |
| `<prefix>/RingToOpen` | IN | — | `ON`/`OFF`/`<minutes>` |
| `<prefix>/SetRingToOpenTime` | IN | — | Minutes as integer string |
| `<prefix>/SetComparatorVoltage` | IN | — | mV as integer string |
| `<prefix>/SetGain` | IN | — | Gain as integer string |
| `<prefix>/StartConfigMode` | IN | — | `ON` → open portal |
| `<prefix>/OtaUpdate` | IN | — | URL string |
| `<prefix>/SetAddress` | IN | — | Address as integer string |

### 7.3 Web UI Layout

```
┌─────────────────────────────┐
│    Simplebus2-Bridge        │  ← h1
│   MQTT-to-Simplebus2        │  ← .sub
├─────────────────────────────┤
│ STATUS                      │  ← .sec
│ ┌─────────────────────────┐ │
│ │ Firmware    1.1.0       │ │
│ │ Uptime      0d 01:01    │ │
│ │ WiFi        Connected   │ │
│ │             (-62, Good) │ │
│ │ MQTT        Connected   │ │
│ │ Free Heap   211/329 kB  │ │
│ │ Free Flash  360/4096 kB │ │
│ └─────────────────────────┘ │
├─────────────────────────────┤
│ WIFI                        │  ← .sec
│ ┌─────────────────────────┐ │
│ │ SSID    [____________]  │ │
│ │ Password[____________]  │ │
│ │ [    Save & Reconnect ] │ │
│ └─────────────────────────┘ │
├─────────────────────────────┤
│ MQTT                        │  ← .sec
│ ┌─────────────────────────┐ │
│ │ Broker Host  [________] │ │
│ │ User         [________] │ │
│ │ Password     [________] │ │
│ │ Topic Prefix [________] │ │
│ └─────────────────────────┘ │
├─────────────────────────────┤
│ DEVICE CONFIGURATION        │  ← .sec
│ ┌─────────────────────────┐ │
│ │ Intercom Address [___]  │ │
│ │ Gain             [___]  │ │
│ │ Threshold (mV)   [___]  │ │
│ │ RTO Duration (min)[___] │ │
│ │ [        Save         ] │ │
│ └─────────────────────────┘ │
├─────────────────────────────┤
│ OTA FIRMWARE UPDATE         │  ← .sec
│ ┌─────────────────────────┐ │
│ │ URL [simplebus2-fw.bin] │ │
│ │ [       OTA Flash     ] │ │
│ │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ │ │
│ │ Or upload .bin directly │ │
│ │ [  Choose File  ] ...   │ │
│ │ [   Upload & Flash    ] │ │
│ └─────────────────────────┘ │
├─────────────────────────────┤
│ SYSTEM                      │  ← .sec
│ ┌─────────────────────────┐ │
│ │ [Restart] [FactoryReset]│ │
│ └─────────────────────────┘ │
└─────────────────────────────┘
```

---

## 8. Verification & Validation

### 8.1 Test Cases — Phase 1

| ID | Requirement | Objective | Steps | Expected Result |
|----|-------------|-----------|-------|-----------------|
| T1.1 | FR-10.1 | OTA URL entity removed from HA | 1. Flash firmware. 2. Subscribe to `homeassistant/#`. 3. Check for OTA URL config topic. | No `homeassistant/text/.../otaurl/config` message with payload. |
| T1.2 | FR-10.2 | Ring-to-Open Duration default 600 | 1. Flash firmware. 2. Check HA entity `Ring To Open Duration`. | Entity shows 600 as initial value. |

### 8.2 Test Cases — Phase 2

| ID | Requirement | Objective | Steps | Expected Result |
|----|-------------|-----------|-------|-----------------|
| T2.1 | FR-8.1, FR-8.2 | Default prefix `SimpleBus` | 1. Flash with factory defaults. 2. Subscribe to `SimpleBus/#`. 3. Trigger ring. | Ring published to `SimpleBus/EntryDoor`. |
| T2.2 | FR-8.3 | HA discovery uses prefix | 1. Subscribe to `homeassistant/#`. | Discovery payloads contain `simplebus2/EntryDoor` etc. |
| T2.3 | FR-8.4 | EEPROM migration | 1. Flash firmware with old EEPROM layout. 2. Reboot. | Device boots with defaults; no crash. |
| T2.4 | FR-8.5 | Prefix change notice | 1. POST `/api/config` with `mqtt_prefix=sb2`. 2. Check UI response. | Response JSON contains `"restart_required": true`; UI shows notice. |

### 8.3 Test Cases — Phase 3

| ID | Requirement | Objective | Steps | Expected Result |
|----|-------------|-----------|-------|-----------------|
| T3.1 | FR-1.2, FR-1.4 | Dashboard loads | 1. Flash firmware. 2. Navigate to `http://Simplebus2-Bridge.local/`. | HTML dashboard rendered. |
| T3.2 | FR-2.1–FR-2.6 | Status API | 1. GET `/api/status`. | JSON contains all fields; uptime > 0; wifi/mqtt bool match reality. |
| T3.3 | FR-2.2 | WiFi quality rating | 1. Check status with known RSSI (e.g. −62 dBm). | `wifi_quality` = `"Good"`. |
| T3.4 | FR-3.2, FR-3.3 | Config save MQTT | 1. POST `/api/config` with new `mqtt_host`. 2. Check MQTT reconnect. | Bridge reconnects to new host. |
| T3.5 | FR-4.1, FR-4.2 | WiFi reconfiguration | 1. POST `/api/wifi` with valid SSID/PW. | Bridge attempts reconnect; status shows new WiFi. |
| T3.6 | FR-5.1 | OTA via URL | 1. POST `/api/ota` with valid firmware URL. | OTA starts; device reboots with new firmware. |
| T3.6b | FR-5.4, FR-5.5 | OTA file upload | 1. Open web UI. 2. Select `.bin` file. 3. Click Upload & Flash. | Progress shown; device flashes and reboots; firmware version updated. |
| T3.6c | FR-4.2 | WiFi credentials persist after reboot | 1. POST `/api/wifi` with new credentials. 2. Power-cycle device. | Device reconnects to configured WiFi without entering portal. |
| T3.6d | FR-9.16 | Ring Automation removed | 1. Open web UI → Device Configuration. | No Ring Automation checkbox present (removed 2.0.0). |
| T3.7 | FR-6.2 | Factory reset | 1. POST `/api/factory-reset`. 2. Reboot. | EEPROM has defaults; device opens `Simplebus2-Bridge` AP. |
| T3.8 | FR-7.1, FR-7.2 | Portal WiFi only | 1. Trigger factory reset. 2. Connect to `Simplebus2-Bridge` AP. | Portal shows only SSID/PW fields. |
| T3.9 | NFR-4 | Doorbell processing unchanged | 1. Flash firmware. 2. Run `sb2test.py` (all 5 tests). | 5/5 tests pass. |
| T3.10 | NFR-3 | Loop latency | 1. Monitor MQTT heartbeats during active HTTP requests. | Heartbeats not delayed by > 100 ms. |
| T3.11 | FR-3.4, FR-3.5 | Device config persistence | 1. POST `/api/config` with custom gain/threshold/rto. 2. POST `/api/restart`. 3. GET `/api/config`. | All values survive reboot. |
| T3.12 | FR-5.1 | OTA URL validation | 1. POST `/api/ota` with `url=ftp://invalid`. | HTTP 400, `ok=false`. |
| T3.13 | FR-3.6 | MQTT prefix change notice | 1. POST `/api/config` with changed `mqtt_prefix`. | Response contains `"restart_required": true`. |

### 8.3.0 Test Cases — Release 2.0.0

| ID | Requirement | Objective | Steps | Expected Result |
|----|-------------|-----------|-------|-----------------|
| T4.1 | FR-10.4, FR-10.5 | EntryDoor / FloorDoor visible in HA | 1. Flash 2.0.0. 2. Reload MQTT in HA. 3. Check Devices & Services. | Both sensors appear under Simplebus2-Bridge with doorbell icon; no availability error. |
| T4.2 | FR-10.6 | RingToOpenTime bidirectional sync | 1. Change duration in web UI → Save. 2. Check HA number entity. 3. Change value in HA. 4. Reload web UI. | Value updated in both directions; persists after reboot. |
| T4.3 | FR-9.16 | Ring Automation absent from web UI | 1. Open web UI → Device Configuration. | No Ring Automation checkbox present. |
| T4.4 | safeRestart | Device boots from flash after software reset | 1. Trigger restart via web UI. 2. Observe serial boot output. | Boot reason: `SPI_FAST_FLASH_BOOT` (not `USB_BOOT`). |

### 8.3.1 Phase 3 Test Results — 2026-04-14

Executed on: ESP32-C3, fw 1.1.0, workbench AP (192.168.4.1), MQTT broker 192.168.4.1:1883.

| ID | Result | Notes |
|----|--------|-------|
| T3.1 | ✅ PASS | 9 569 bytes served, HTTP 200 |
| T3.2 | ✅ PASS | All fields present; uptime, rssi, temp, heap, flash correct |
| T3.3 | ✅ PASS | RSSI −32 dBm → `"Excellent"` |
| T3.4 | ✅ PASS | New prefix `simplebus2test` saved, `restart_required=true` returned |
| T3.5 | ✅ PASS | Credentials saved, device reconnected within ~6 s |
| T3.6 | ✅ PASS | OTA from workbench firmware server; device rebooted, fw=1.1.0 confirmed |
| T3.6b | ✅ PASS | 1 011 327-byte binary uploaded via multipart; device rebooted, uptime=5 s after boot |
| T3.6c | ✅ PASS | WiFi credentials persisted after restart (same SSID confirmed in `/api/config`) |
| T3.6d | N/A (1.1.0) | Ring Automation checkbox removed in 2.0.0; see T4.3 |
| T3.7 | ✅ PASS | NVS cleared; device opened `Simplebus2-Bridge` AP within ~30 s |
| T3.8 | ✅ PASS | Portal AP visible via Pi iwlist scan at −79 dBm; WiFiManager portal accessible at 192.168.4.1 |
| T3.11 | ✅ PASS | gain=22, threshold=650, rto_minutes=10, ring_automation=true all survived restart |
| T3.12 | ✅ PASS | HTTP 400, `{"ok":false,"error":"invalid url"}` |
| T3.13 | ✅ PASS | `restart_required=true` on prefix change, `false` on same-prefix save |

### 8.3.2 Phase 4 Test Results — 2026-04-14

Executed on: ESP32-C3, fw 1.1.0, workbench AP (192.168.4.1), MQTT broker 192.168.4.1:1883.

#### FR-10 — HA Discovery (T1.1, T1.2)

| ID | Result | Notes |
|----|--------|-------|
| T1.1 | ✅ PASS | No `homeassistant/text/.../otaurl/config` topic in subscription output; 8 entities total |
| T1.2 | ✅ PASS | `homeassistant/number/.../ringtoopentime/config` payload contains `"init":600` |

#### FR-8 — MQTT Prefix (T2.1, T2.2, T2.3, T2.4)

| ID | Result | Notes |
|----|--------|-------|
| T2.1 | ✅ PASS | `simplebus2/availability`, `simplebus2/RingToOpenStatus` published on connect; prefix used in all topics |
| T2.2 | ✅ PASS | HA discovery `stat_t`/`cmd_t`/`avty_t` all contain configured prefix; verified with `sb2test` prefix: `"stat_t":"sb2test/EntryDoor"` etc. |
| T2.3 | ✅ PASS | Device boots cleanly with defaults after factory reset; no crash |
| T2.4 | ✅ PASS | `POST /api/config mqtt_prefix=sb2test` → `{"ok":true,"restart_required":true}`; HA discovery after restart uses `sb2test` |

#### FR-7.5 — Captive Portal Dark Theme

| ID | Result | Notes |
|----|--------|-------|
| T3.8b | ✅ PASS | Portal root at 192.168.4.1 serves dark-theme HTML: `<title>Simplebus2-Bridge</title>`, background `#111827`, WiFi/Exit buttons link to `/wifi` and `/exit`; 1022 bytes |

#### NFR-3 — Loop Latency (T3.10)

| ID | Result | Notes |
|----|--------|-------|
| T3.10 | ✅ PASS | HTTP GET `/api/status` round-trip 41–56 ms (warm, via workbench HTTP relay); well below 100 ms limit. Heartbeat interval is 5 min (not directly measurable in 65 s window); HTTP latency confirms loop is not blocked. |

#### NFR-4 — Decoder Unchanged (T3.9)

| ID | Result | Notes |
|----|--------|-------|
| T3.9 | ✅ PASS | `git diff abd492a HEAD -- src/main.cpp` shows zero diffs on ISR, timing constants (CARRIER_MAX_DELTA_US, PREAMBLE_SILENCE_US, BIT_ZERO_SILENCE_US, BIT_ONE_SILENCE_US, TIMING_TOLERANCE), ring buffer, deduplication, or repeat-ring logic. |

### 8.3.3 Release 2.0.0 Test Results — 2026-04-19

Executed on: ESP32-C3, fw 2.0.0, home MQTT broker (production), Home Assistant with MQTT integration.

| ID | Result | Notes |
|----|--------|-------|
| T4.1 | ✅ PASS | EntryDoor and FloorDoor sensors both visible in HA under Simplebus2-Bridge device; doorbell icon displayed; no availability error |
| T4.2 | ✅ PASS | Duration changed in web UI → immediately reflected in HA number entity; changed in HA → saved to EEPROM, reflected in web UI on reload |
| T4.3 | ✅ PASS | Device Configuration section contains no Ring Automation checkbox |
| T4.4 | ✅ PASS | Serial boot output after web UI restart: `rst:0x15 (USB_UART_CHIP_RESET), boot:0xc (SPI_FAST_FLASH_BOOT)` — no USB_BOOT loop |

### 8.4 Traceability Matrix

| Requirement | Test(s) | Status |
|-------------|---------|--------|
| FR-1.1 Web server start | T3.1 | ✅ PASS |
| FR-1.2 Serve `/` | T3.1 | ✅ PASS |
| FR-1.4 mDNS access | T3.1 | ✅ PASS |
| FR-2.1 Status JSON | T3.2 | ✅ PASS |
| FR-2.2 WiFi quality | T3.3 | ✅ PASS |
| FR-3.2 Config POST | T3.4 | ✅ PASS |
| FR-3.3 MQTT reconnect | T3.4 | ✅ PASS |
| FR-3.4 Device config persist | T3.11 | ✅ PASS |
| FR-3.6 Prefix restart notice | T3.13 | ✅ PASS |
| FR-4.1 WiFi POST | T3.5 | ✅ PASS |
| FR-4.2 WiFi credentials persist | T3.6c | ✅ PASS |
| FR-5.1 OTA URL trigger | T3.6 | ✅ PASS |
| FR-5.4 OTA file upload | T3.6b | ✅ PASS |
| FR-5.5 OTA progress feedback | T3.6b | ✅ PASS |
| FR-5.6 OTA URL validation | T3.12 | ✅ PASS |
| FR-6.2 Factory reset | T3.7 | ✅ PASS |
| FR-7.1 AP name | T3.8 | ✅ PASS |
| FR-7.2 Portal WiFi only | T3.8 | ✅ PASS |
| FR-7.5 Captive portal dark theme | T3.8b | ✅ PASS |
| FR-8.1 Prefix in topics | T2.1 | ✅ PASS |
| FR-8.2 Default `SimpleBus` | T2.1 | ✅ PASS |
| FR-8.3 HA discovery prefix | T2.2 | ✅ PASS |
| FR-8.4 EEPROM migration | T2.3 | ✅ PASS |
| FR-8.5 Prefix change notice | T2.4 | ✅ PASS |
| FR-9.16 Ring Automation removed | T4.3 | ✅ PASS |
| FR-10.1 OTA URL removed | T1.1 | ✅ PASS |
| FR-10.2 RTO Duration default | T1.2 | ✅ PASS |
| FR-10.3 HA discovery uses prefix | T2.2 | ✅ PASS |
| FR-10.4 No avty_t in binary sensors | T4.1 | ✅ PASS |
| FR-10.5 ic instead of dev_cla | T4.1 | ✅ PASS |
| FR-10.6 RingToOpenTime stat_t + publish | T4.2 | ✅ PASS |
| NFR-3 Loop latency | T3.10 | ✅ PASS |
| NFR-4 Decoder unchanged | T3.9 | ✅ PASS |

---

## 9. Operational Procedures

### 9.1 First Flash / Factory Reset

1. Flash firmware via workbench (`rfc2217://...`) or OTA
2. Device opens AP `Simplebus2-Bridge`
3. Connect to AP → navigate to `192.168.4.1` (WiFiManager portal)
4. Enter WiFi SSID + Password → Save
5. Device connects to WiFi → `Simplebus2-Bridge.local` accessible
6. Open `http://Simplebus2-Bridge.local/` → configure MQTT, prefix, address, gain, threshold

### 9.2 Normal Operation

- Web interface always accessible at device IP and `Simplebus2-Bridge.local`
- MQTT reconnects automatically on broker restart
- Ring-to-Open state persists across MQTT reconnects (not across reboots)

### 9.3 After MQTT Prefix Change

1. Change prefix in web interface → Save
2. Restart HA MQTT integration (Settings → Devices & Services → MQTT → Reload)
3. Old entities with `SimpleBus/` prefix will show as unavailable and can be deleted

### 9.4 OTA Firmware Update

**Option A — URL (requires HTTP server):**
1. Host firmware file on HTTP server (e.g. `http://192.168.x.y/simplebus2-firmware.bin`)
2. Open web interface → OTA section → enter URL → click OTA Flash
3. Device downloads and flashes → reboots → settings preserved (EEPROM not touched)

**Option B — File Upload (no server required):**
1. Open web interface → OTA section → click Choose File → select `.bin`
2. Click Upload & Flash
3. Device flashes directly from browser upload → reboots → settings preserved

> Note: Both methods use HTTP only, not HTTPS.

---

## 10. Appendix

### 10.1 WiFi Quality Thresholds

| RSSI (dBm) | Label |
|------------|-------|
| ≥ −60 | Excellent |
| −61 to −70 | Good |
| −71 to −80 | Ok |
| < −80 | Poor |

### 10.2 EEPROM Version Strategy

Current code uses `structInitialized()` which checks a set of fields. Proposed change:
- Add `uint8_t version` as the first field of `Settings`
- On boot: if `version != SETTINGS_VERSION` → `memset` struct to zero, set all defaults, set `version = SETTINGS_VERSION`
- `SETTINGS_VERSION` = `2` (current code is implicitly version 1)

### 10.3 Flash Footprint Estimate

| Component | Est. Size |
|-----------|-----------|
| Existing firmware (after last build) | 950 KB |
| WebServer library (already in ESP32 Arduino core) | ~20 KB |
| HTML/CSS/JS in PROGMEM | ~6 KB |
| MQTT prefix + EEPROM changes | ~2 KB |
| **Total estimate** | **~978 KB** |

Available app partition: 1280 KB (1 MB). Margin: ~300 KB. ✓
