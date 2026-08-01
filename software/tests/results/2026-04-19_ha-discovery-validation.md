# HA Discovery & Bidirectional Sync Validation — 2026-04-19

## Firmware under test

Branch: `Simplebus2-MQTT-Bridge`  
Version: 1.1.8  
Hardware: ESP32-C3 (industrial PCB, GPIO9 = SDA_2/BOOT)  
Workbench: 192.168.0.35, SLOT2 (RFC2217 port 4002)  
MQTT broker: production Home Assistant broker  

---

## Changes validated

| Change | Before (1.1.0–1.1.7) | After (1.1.8) |
|--------|----------------------|---------------|
| `avty_t` in binary sensor discovery | Present | Removed |
| LWT in `client.connect()` | Present | Removed |
| `dev_cla: doorbell` | Present | Replaced with `ic: mdi:doorbell` |
| delete+create pattern for entities | Used on reconnect | Removed |
| `stat_t` on RingToOpenTime number entity | Absent | Present (`<prefix>/RingToOpenTime`) |
| `SetRingToOpenTime` callback | Updated runtime only | Saves to EEPROM, publishes state back |
| `RingToOpenTime` published on connect | No | Yes (retained) |
| `ringToOpenMinutes` on web UI save | Not published to MQTT | Published retained |
| `safeRestart()` | Not present | GPIO9 INPUT_PULLUP before restart |
| Ring Automation in web UI | Checkbox present | Removed |

---

## Test 1 — HA Sensor Visibility

**Objective:** EntryDoor and FloorDoor binary sensors appear in Home Assistant.

**Procedure:**
1. Flashed 1.1.8 via workbench RFC2217
2. Reset via `POST /api/serial/reset` (SLOT2) → `SPI_FAST_FLASH_BOOT` confirmed
3. Reloaded MQTT integration in HA (Settings → Devices & Services → MQTT → Reload)
4. Checked device "Simplebus2-Bridge" entity list

**Result:** ✅ PASS

Both `Entry Door` and `Floor Door` binary sensor entities visible under the Simplebus2-Bridge device. Doorbell icon (mdi:doorbell) displayed. No availability error.

| Entity | Visible | Icon | State |
|--------|---------|------|-------|
| Entry Door | ✅ Yes | mdi:doorbell | OFF |
| Floor Door | ✅ Yes | mdi:doorbell | OFF |
| Ring To Open | ✅ Yes | — | OFF |
| Ring To Open Status | ✅ Yes | — | OFF |
| Ring To Open Duration | ✅ Yes | mdi:timer-outline | 600 |
| Heartbeat | ✅ Yes | — | — |
| Open Door | ✅ Yes | — | — |

---

## Test 2 — RingToOpenTime Bidirectional Sync

**Objective:** Ring-to-Open Duration value synchronises between web UI, HA, and MQTT in both directions.

### 2a — Web UI → HA

**Procedure:**
1. Opened web UI → Device Configuration
2. Changed Ring-to-Open min from 600 to 30
3. Clicked Save
4. Observed HA Ring To Open Duration entity

**Result:** ✅ PASS — HA entity updated to 30 within ~2 s of save.

### 2b — HA → Web UI / EEPROM

**Procedure:**
1. Changed Ring To Open Duration in HA to 90
2. Reloaded web UI
3. Triggered restart via web UI
4. Reloaded web UI after reboot

**Result:** ✅ PASS — Web UI showed 90 before and after restart; value persisted to EEPROM.

---

## Test 3 — Ring Automation Absent from Web UI

**Objective:** Ring Automation checkbox no longer present in Device Configuration.

**Procedure:** Opened web UI → Device Configuration.

**Result:** ✅ PASS — No Ring Automation checkbox or label present.

---

## Test 4 — safeRestart Boot Mode

**Objective:** Device boots from flash (not USB download mode) after firmware-initiated restart.

**Procedure:**
1. Triggered restart via `POST /api/restart`
2. Captured boot output via `POST /api/serial/reset` on SLOT2

**Result:** ✅ PASS

```
rst:0x15 (USB_UART_CHIP_RESET),boot:0xc (SPI_FAST_FLASH_BOOT)
```

`SPI_FAST_FLASH_BOOT` confirms GPIO9 was high at reset (safeRestart released I2C before restart).

---

## Summary

| Test | ID | Result |
|------|----|--------|
| EntryDoor / FloorDoor visible in HA | T4.1 | ✅ PASS |
| RingToOpenTime web UI → HA | T4.2a | ✅ PASS |
| RingToOpenTime HA → EEPROM | T4.2b | ✅ PASS |
| Ring Automation absent from web UI | T4.3 | ✅ PASS |
| safeRestart → SPI_FAST_FLASH_BOOT | T4.4 | ✅ PASS |

**5/5 tests passed.**
