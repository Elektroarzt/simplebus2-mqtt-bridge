# Ring Detection Validation — 2026-04-20

## Firmware under test

Branch: `Simplebus2-MQTT-Bridge`  
Version: 2.0.0  
Hardware: ESP32-C3 (industrial PCB)  
Workbench: 192.168.0.35, bridge at 192.168.4.4 (workbench AP)  
MQTT broker: workbench mosquitto (192.168.4.1:1883)  
Test fixture: 8-ch relay board (Relais-Board-8x, 192.168.4.3)  
Intercom: real Simplebus2 system connected to bridge GPIO2, address 9

---

## Test setup

- Relay 2 (NO contact) closes the entry door bell button (Klingel Aussen)
- Relay 1 (NO contact) closes the floor door bell button (Klingel Innen)
- Relay fires a 300 ms pulse (PulseTime = 3) to simulate a button press
- Real intercom generates authentic Simplebus2 modulated bus signal
- Bridge decodes signal via ISR on GPIO2, filters by address 9, publishes to MQTT

---

## Test T5.1 — EntryDoor ring detection

**Objective:** Pressing Klingel Aussen (via Relay 2) triggers `simplebus2/EntryDoor = ON` on MQTT.

**Procedure:**
1. Fresh bridge boot (all counters at 0)
2. MQTT subscriber running on `simplebus2/#`
3. `PulseTime2 3` set on relay board
4. `Power2 ON` sent via workbench HTTP relay

**Result:** ✅ PASS

```
[LIVE] simplebus2/EntryDoor = ON
```

| Counter | Before | After |
|---------|--------|-------|
| dbg_isr | 0 | 3550 |
| dbg_decode_ok | 0 | 1 |
| dbg_addr_mismatch | 0 | 0 |
| dbg_mqtt_published | 0 | 1 |
| dbg_last_addr | 0 | 9 |
| dbg_last_code | 0 | 50 |

---

## Test T5.2 — FloorDoor ring detection

**Objective:** Pressing Klingel Innen (via Relay 1) triggers `simplebus2/FloorDoor = ON` on MQTT.

**Procedure:**
1. Waited 87 s for anti-spam timer to clear after T5.1
2. MQTT subscriber running on `simplebus2/#`
3. `PulseTime1 3` set on relay board
4. `Power1 ON` sent via workbench HTTP relay

**Result:** ✅ PASS

`dbg_mqtt_published` incremented from 1 → 2, `dbg_last_addr: 9`, `dbg_last_code: 21`

---

## Summary

| Test | Result |
|------|--------|
| T5.1 EntryDoor ring → MQTT | ✅ PASS |
| T5.2 FloorDoor ring → MQTT | ✅ PASS |

**2/2 tests passed.**

### Notes

- Anti-spam timer: ~80 s between ring events (firmware enforced)
- `dbg_addr_mismatch` may increment from other Simplebus2 devices on the bus — expected behaviour
- Relay board closes real intercom button contacts; intercom generates authentic Simplebus2 carrier signal — relay closure alone cannot simulate the signal
- Bridge must be physically connected to the Simplebus2 bus for these tests
