# Simplebus2-Bridge — Release Notes

---

## Version 2.0.0 — 2026-08-01

### Overview

Major firmware release versus the previous public sketch on GitHub. The **Simplebus2 bus decoder was redesigned for robustness** (the original reason for this work), and the bridge gained a permanent web UI, configurable MQTT prefix (**default remains `SimpleBus`** for drop-in MQTT compatibility), Home Assistant MQTT discovery, safer restarts on ESP32-C3, and OTA improvements.

---

### Bus decoder (primary change)

Compared with the earlier public `software/main.cpp`, reception on the Simplebus2 line is substantially more reliable:

- **State-machine decoder** (`SimpleBusDecoder`) with explicit preamble / bit timing instead of ad-hoc ISR decoding
- **ISR ring buffer** (128 edges) so the interrupt only queues timestamps; decoding runs in the main loop (less ISR time, fewer missed edges under load)
- **Tighter timing tolerance** (12% vs previously looser windows) to reject noise and false positives
- **Longer duplicate window** (6 s) so the bus’s second frame ~4–5 s after a ring does not publish a double MQTT event
- **Immediate publish** (removed artificial 2 s delay)
- **Address filter, checksum checks, and debug counters** (`dbg_isr`, `dbg_decode_ok`, …) exposed via `/api/status`
- Validated with ring / idle / burst tests (see `software/tests/results/`)

Ring-to-Open still opens **only on Entry Door** rings, not Floor Door.

---

### Upgrade from previous public firmware

**EEPROM device settings are not compatible** with the old sketch (no settings-version field; different struct layout). On first boot the firmware resets EEPROM to defaults. **Re-enter MQTT broker/credentials and device parameters** in the web UI. WiFi credentials (WiFiManager NVS) usually survive.

---

### Bug Fixes

#### HA Auto-Discovery: EntryDoor and FloorDoor sensors not appearing

- **Root cause:** Retained `offline` LWT payload from earlier firmware revisions was still present in the MQTT broker. Because the discovery payloads referenced an `avty_t` (availability topic), Home Assistant hid both binary sensor entities whenever it read that retained `offline`.
- **Fix:** LWT removed from `client.connect()`. `avty_t` removed from all binary sensor discovery payloads. Entities are now unconditionally visible.
- **Additional fix:** `"dev_cla":"doorbell"` replaced with `"ic":"mdi:doorbell"` for compatibility with Home Assistant versions that do not support the `doorbell` device class.

#### safeRestart — GPIO9 / BOOT strapping pin conflict

- **Root cause:** GPIO9 is an ESP32-C3 **strapping pin** (boot mode) and is also used as SDA_2 (I2C to the MCP4017). If the I2C driver holds SDA low at `ESP.restart()`, the chip samples BOOT = LOW and enters USB download mode instead of booting from flash.
- **Fix:** `safeRestart()` helper added. Calls `Wire.end()`, sets `GPIO9` to `INPUT_PULLUP` (SDA released high), waits 10 ms, then calls `ESP.restart()`. All firmware-initiated restarts now use `safeRestart()`. `setPotValue()` also releases I2C after each transaction.

#### RingToOpenTime — not synchronised bidirectionally

- **Root cause (discovery):** The `number` entity in HA had no `stat_t`; HA treated it as optimistic and showed no feedback. The `SetRingToOpenTime` MQTT callback only updated the runtime variable (`ringToOpenMillis`) but did not persist to EEPROM or publish state back.
- **Fix:** `stat_t` (`<prefix>/RingToOpenTime`) added to the `number` entity discovery payload. Firmware publishes the current value (retained) on MQTT connect and on every change via web UI or `SetRingToOpenTime` command. The `SetRingToOpenTime` handler now saves to `mySettings.ringToOpenMinutes` and calls `saveSettings()`.

#### Issue #13 (msgCode / structInitialized)

- `writeMessageToIntercom`: compare with `msgCode == MSG_OPEN_DOOR` (was accidental assignment `=`).
- `structInitialized()`: use settings version + `strlen(mqttServer)` instead of `sizeof` on char arrays.

---

### Changes

#### Ring Automation removed from web interface

The **Enable Ring Automation** checkbox has been removed from the Device Configuration section of the web UI. Ring-to-Open is controlled exclusively via Home Assistant or MQTT (`RingToOpen` topic). The `ringAutomation` field remains in EEPROM; it is no longer writable through the web API.

---

### Flash footprint

```
RAM:   13.4%  (43764 / 327680 bytes)
Flash: 73.8%  (967382 / 1310720 bytes)
```

---

## Version 1.1.1 — 2026-04-15

### Bug Fixes

- **Non-blocking MQTT reconnect:** Reconnect attempts no longer block the main loop; the `reconnect()` function is rate-limited to one attempt per 5 s.
- **Stale retained RingToOpen:** On MQTT connect the firmware publishes an empty retained payload to `<prefix>/RingToOpen` to clear any stale broker-retained ON state from a previous session.
- **Debug counters:** Added `ringCount`, `falsePositiveCount`, and `mqttReconnectCount` internal counters for diagnostics (logged via UDP, not exposed via API).
- **IRAM_ATTR guard:** ISR functions annotated with `IRAM_ATTR` wrapped in a linter-compatibility guard to suppress PlatformIO static-analysis warnings without altering runtime behaviour.

---

## Version 1.1.2 — 2026-04-15

### Bug Fixes

- **HA Discovery: avty_t removed (initial attempt):** Removed `avty_t` from binary sensor discovery payloads to prevent HA hiding entities due to stale retained LWT. (Full fix including LWT removal and `dev_cla` replacement completed in 2.0.0.)

---

## Version 1.1.0 — 2026-04-14

### Overview

Version 1.1.0 introduces a permanent HTTP web interface for configuration and monitoring, replaces the hardcoded `SimpleBus/` MQTT topic prefix with a configurable one, and adds OTA firmware update support (URL and file upload). Bus-decoder robustness work from the same development line is summarized under **2.0.0** (public release date).

---

### New Features

#### Permanent Web Interface (port 80)

A dark-themed single-page dashboard is now available at `http://Simplebus2-Bridge.local/` or the device IP at all times — no need to trigger the WiFiManager captive portal for configuration.

Sections:

| Section | Function |
|---------|----------|
| **Status** | Live firmware version, uptime, WiFi (SSID, RSSI, quality), MQTT state, CPU temperature, heap and flash usage. Auto-refreshes every 5 s. |
| **WiFi** | Change SSID and password. Device reconnects immediately and persists credentials. |
| **MQTT** | Broker host, username, password, topic prefix. Prefix change flags a restart notice. |
| **Device Configuration** | Intercom address (0–255), amplifier gain (2–40), detection threshold mV (100–1500), Ring-to-Open duration (1–1440 min), Ring Automation enable. All values persisted to NVS. |
| **Firmware Update (OTA)** | Option A: enter a firmware URL (HTTP). Option B: upload a local `.bin` file directly from the browser with a progress indicator. |
| **System** | Restart and Factory Reset buttons. |

#### Configurable MQTT Topic Prefix

All MQTT topics previously hardcoded as `SimpleBus/…` are now configurable via the web interface. **Default remains `SimpleBus`** so existing MQTT subscriptions and automations keep working after upgrade.

| Topic (default prefix) | Trigger |
|------------------------|---------|
| `SimpleBus/EntryDoor` | Ring at entrance door (Außenstelle) |
| `SimpleBus/FloorDoor` | Ring at floor door (Innenstelle) |
| `SimpleBus/RingToOpenStatus` | Ring-to-Open active/inactive |
| `SimpleBus/RingToOpenOpened` | Door opened via Ring-to-Open |

HA Discovery topics update to use the configured prefix automatically after a restart.

> **Note:** Changing the prefix requires a device restart to take effect. The UI shows a notice when this is the case.

#### OTA Firmware Update

Two methods are available:

- **URL-based:** Enter any `http://` URL pointing to a `.bin` file. HTTPS and redirects are not supported.
- **File upload:** Select a `.bin` file locally and upload it directly from the browser. A percentage progress bar is shown during upload. The device reboots automatically after successful flash.

#### Ring Automation Label Clarification

The Ring Automation checkbox label now reads: *"Enable Ring Automation — must also be set via Home Assistant or MQTT"*, making clear that the feature requires configuration on both sides.

---

### Changes vs. Version 1.0.x

| Area | Before (1.0.x) | After (1.1.0) |
|------|----------------|---------------|
| Configuration UI | WiFiManager captive portal only (AP mode, temporary) | Permanent web interface at port 80, always accessible |
| MQTT topic prefix | Hardcoded `SimpleBus/` | Configurable, default still `SimpleBus` |
| WiFi reconfiguration | Requires factory reset + portal trigger | Available via web interface at any time |
| Device settings (gain, threshold, etc.) | WiFiManager portal parameters only | Web interface Device Configuration section |
| OTA update | URL-based only (via MQTT message `SimpleBus/OtaUpdate`) | URL via web interface + file upload via browser |
| WiFi credential persistence | Not guaranteed after power cycle | `WiFi.persistent(true)` + `ESP.restart()` — persisted to NVS |
| mDNS hostname | `Simplebus2-Bridge.local` (already present) | Unchanged |
| Simplebus2 decoder | — | Unchanged |
| HA Discovery | 9 entities; OTA URL entity present; RTO Duration no default | OTA URL entity removed; RTO Duration defaults to 600 min |

---

### Bug Fixes

- **WiFi credentials not persisted after restart:** `WiFi.persistent(true)` is now called before `WiFi.begin()` in `handlePostWifi()`. Previously, a cold boot after restart would re-enter the WiFiManager portal.
- **WebServer port conflict:** The WebServer is now stopped before launching the WiFiManager portal, preventing a port 80 bind error that caused the portal to silently fail.
- **EEPROM layout migration (v1 → v2 → v3):** Settings struct includes a version field and magic number. On mismatch, defaults are applied and the struct is re-written cleanly.

---

### Tested

All 13 web interface tests passed on ESP32-C3 hardware (fw 1.1.0, 2026-04-14). 3-hour multi-phase endurance test (8 phases, P1–P8) passed 8/8. See `simplebus2-bridge-webui-fsd.md` section 8 for details.

---

## Version 1.0.x — Prior Releases

### 1.0.2 — Stability & HA Discovery Fixes

- Debounce window extended (DUPLICATE_WINDOW_MS 3000 → 6000 ms) to cover second Simplebus2 frame at 4–5 s
- Timing tolerance tightened (20 % → 12 %) to reject noise-induced false positives
- Removed artificial 2 s publish delay — events published immediately
- HA MQTT Auto Discovery: 9 entities, LWT availability, `off_delay`
- HW anti-spam confirmed: second ring blocked within 80 s window

### 1.0.1 — MQTT Prefix + EEPROM v2

- Configurable MQTT topic prefix via WiFiManager portal parameter (precursor to 1.1.0 web interface)
- EEPROM v2: added `mqttPrefix` (32 bytes) and `ringToOpenMinutes` fields
- EEPROM migration: graceful upgrade from v1 layout

### 1.0.0 — Initial Release

- Simplebus2 bus decoder (ISR, ring buffer, preamble + bit timing)
- MQTT publish for EntryDoor, FloorDoor, RingToOpen events
- WiFiManager captive portal for all settings
- Deduplication window, HW anti-spam filter
- OTA via MQTT message (`SimpleBus/OtaUpdate`)
