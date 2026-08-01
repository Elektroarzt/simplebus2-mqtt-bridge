# Firmware Validation — 2026-04-13

## Firmware under test

Branch: `Simplebus2-MQTT-Bridge`  
Commit: after fix for 2×/ring bug, false positives, publish latency, HA Auto Discovery

### Changes validated

| Change | Before | After |
|--------|--------|-------|
| `DUPLICATE_WINDOW_MS` | 3000 ms | 6000 ms |
| `TIMING_TOLERANCE` | 20% | 12% |
| Publish latency | 2 s artificial delay | immediate |
| HA Auto Discovery | — | 9 entities, LWT, off_delay |

### Hardware

- DUT: ESP32-C3, IP 192.168.0.201, connected to home MQTT broker
- Stimulus: 8-channel relay board (Relais-Board-8x) via workbench AP
  - Relay 1 → FloorDoor (Simplebus2 code 21)
  - Relay 2 → EntryDoor (Simplebus2 code 48)
- Ring interval: 80 s (> 60 s HW anti-spam timer on physical doorbell)
- MQTT broker: mosquitto on workbench Pi (192.168.0.35)

---

## Test 1 — Quicktest (~20 min, 22:49–23:01)

Script: `/tmp/quicktest.sh`

| Phase | Description | Expected | Result |
|-------|-------------|----------|--------|
| A | 5 rings × 75 s | 5× EntryDoor (1×/ring) | ✅ PASS 5/5 |
| B | 5 min idle | 0 false positives | ✅ PASS 0 FP |
| C | Burst 5 rings in 5 s | 1× EntryDoor | ✅ PASS 1/1 |

**Primary fixes confirmed:** 2×/ring bug fixed, false positives eliminated.

---

## Test 2 — Smoke Test (~8 min, 00:09–00:17)

Script: `tests/smoketest.sh`  
Purpose: validate test infrastructure (relay connectivity, mosquitto_sub reconnect after broker restart)

| Phase | Description | Expected | Result |
|-------|-------------|----------|--------|
| P1 | 1 ring | 1× EntryDoor | ✅ PASS 1/1 |
| P2 | 2 min idle | 0 FP | ✅ PASS |
| P3 | MQTT broker stop 30 s → restart | offline=1 online=1 | ✅ PASS |
| P4 | 1 ring after MQTT reconnect | 1× EntryDoor | ✅ PASS 1/1 |
| P5 | Burst 3 rings in 3 s | 1× EntryDoor | ✅ PASS 1/1 |
| P6 | 2 min idle | 0 FP | ✅ PASS |

**Infrastructure validated.** mosquitto_sub explicit restart after broker stop confirmed working.

---

## Test 3 — 6h30min Validation (00:26–06:55)

Script: `tests/val390.sh`

| Phase | Description | Expected | Result |
|-------|-------------|----------|--------|
| P1 | 50× Entry + 50× Floor × 80 s | entry=50/50 floor=50/50 | ✅ PASS |
| P2 | 60 min idle | 0 FP | ✅ PASS |
| P3 | MQTT stop/start #1 — LWT + reconnect | offline=1 online=1 | ✅ PASS |
| P4 | 21× Entry + 21× Floor × 80 s after reconnect | entry=21/21 floor=21/21 | ✅ PASS |
| P5 | 60 min idle | 0 FP | ✅ PASS |
| P6 | MQTT stop/start #2 — LWT + reconnect | offline=1 online=1 | ✅ PASS |
| P7 | 21× Entry + 21× Floor × 80 s after second reconnect | entry=21/21 floor=21/21 | ✅ PASS |
| P8 | 19 min idle | 0 FP | ✅ PASS |

### MQTT event totals

| Event | Expected | Detected | Rate |
|-------|----------|----------|------|
| `SimpleBus/EntryDoor ON` | 92 | 92 | 100% |
| `SimpleBus/FloorDoor ON` | 92 | 92 | 100% |
| `SimpleBus/availability offline` | 2 | 2 | 100% |
| `SimpleBus/availability online` | 3 | 3 | 100% |
| False positives (Entry + Floor, idle phases) | 0 | 0 | — |

### Idle time summary

| Phase | Duration | False positives |
|-------|----------|----------------|
| P2 | 60 min | 0 |
| P5 | 60 min | 0 |
| P8 | 19 min | 0 |
| **Total** | **139 min** | **0** |

---

## Known issues / not tested

- OTA ring miss (from prior endurance test P9): ring after OTA reboot not detected.
  Suspected cause: bridge not yet subscribed when ring was triggered. Needs separate test.
- Burst test excluded from 6h30min run by design (requires separate investigation).
- HA Auto Discovery payload delivery not verified via MQTT subscribe
  (homeassistant/# not captured in SimpleBus/# wildcard subscription).

---

## Lessons learned (test infrastructure)

- `mosquitto_sub` does **not** auto-reconnect after broker restart — must kill and restart explicitly.
- Command substitution `$(fn ...)` captures all stdout including `log()/tee` output → use global
  variables or temp files for pass/fail results inside helper functions.
- `jq` not available on Pi Zero — use Python3 for JSON/base64 decoding.
- Ring interval must be ≥ 80 s (HW anti-spam ~60 s). At 45 s only first ring is detected.
- Entry door and floor door have **separate** anti-spam timers — alternating at 80 s each is safe.
- Always run a smoke test (~10 min) to validate infrastructure before committing to a long run.
