# How It Works — Float Mission Internals

This document explains how the float firmware autonomously runs the MATE Floats 2026 mission. It covers the state machine, the buoyancy engine, the depth coordinate system, and the failsafe layers.

For the mission spec itself (scoring, packet format, regulations), see [`2026_MATE_Floats_분석.md`](2026_MATE_Floats_분석.md).
For two-board operating procedure, see [`prerequisites.md`](prerequisites.md).

---

## 1. The mission, in one paragraph

When the operator presses `S` at the station, the float runs **3 vertical profiles** without further intervention. Each profile is: descend to **2.5 m** (bottom-of-float reference) and hold for **30 seconds** (±33 cm allowed band), then rise to **40 cm** (top-of-float reference) and hold for **30 seconds**, then return to the surface so the next profile can start. The whole sequence takes ~3 minutes in fake-depth bench tests; in water the descent and rise legs add real travel time.

---

## 2. State machine

The mission runs as a non-blocking state machine inside `loop()` — the depth sensor, packet emitter, BOOT button, and ESP-NOW receiver all keep ticking in parallel.

```
              ┌──────────┐
              │  IDLE    │ ◄──── 'X' (abort) from any state
              └────┬─────┘
                   │ 'S' (start)
                   ▼
           ┌───────────────┐
           │   DESCEND     │  motor full intake — sink fast
           └───────┬───────┘
                   │ depth ≥ 2.27 m  (DEEP_MIN_M)
                   ▼
           ┌───────────────┐
           │  HOLD_DEEP    │  bang-bang at 2.55 m midpoint, 30 s timer
           └───────┬───────┘
                   │ 30 s elapsed in band [2.27, 2.83]
                   ▼
           ┌───────────────┐
           │ ASCEND_SHALLOW│  motor OFF — rise passively
           └───────┬───────┘
                   │ depth ≤ 1.0348 m  (SHALLOW_MAX_M, bottom-ref)
                   ▼
           ┌───────────────┐
           │ HOLD_SHALLOW  │  bang-bang at 0.70 m midpoint, 30 s timer
           └───────┬───────┘
                   │ 30 s elapsed in band [0.3748, 1.0348]
                   ▼
           ┌───────────────┐
           │   SURFACE     │  motor OFF — rise passively
           └───────┬───────┘
                   │ depth ≤ 0.20 m  (SURFACE_M, bottom-ref)
                   ▼
            profileIndex++
                   │
                   ├─ if more profiles → DESCEND
                   └─ else              → DONE
```

### Why SURFACE matters

SURFACE is the **only path to the next profile**. If the surface check never fires, the float stays in `MS_SURFACE` forever, no `[STATE] -> DESCEND (profile N/3)` transition ever logs, and the mission never completes. The check uses bottom-referenced depth (`reportedDepth() ≤ SURFACE_M`); with the sensor at the float's midpoint, the bottom drops to roughly half the float height (~15 cm) once the float is in its natural floating position, comfortably below the 20 cm threshold.

---

## 3. Depth coordinate system

The float is **12 inches (30.48 cm) tall**, and the MS5837 depth sensor is mounted at the **6 in midpoint**.

```
   ━━━━━ ← top
  ┃ 6   ┃
  ┃ in  ┃
  ┃  ●  ┃ ← sensor (midpoint, reads bottom - 0.1524 m)
  ┃ 6   ┃
  ┃ in  ┃
   ━━━━━ ← bottom (= sensor + 0.1524 m)
```

A single helper translates the raw sensor value:

| Function | Returns | Used for |
|---|---|---|
| `reportedDepth()` | sensor + 0.1524 m (bottom of float) | All HOLD bands, SURFACE check, packet output, graph |

The mission specifies hold targets in **mixed references** (2.5 m at the bottom, 40 cm at the top), but internally we normalize everything to bottom depth via `reportedDepth()` for consistent band comparisons. SURFACE detection also uses bottom depth (`reportedDepth() ≤ 0.20 m`) — mid-mounting keeps the sensor submerged across the entire HOLD_SHALLOW band so the depth signal stays continuous and never saturates at 0 m the way a top-mounted sensor would.

### Mission bands (bottom-referenced, in code)

```c
DEEP_MIN_M    = 2.27 m   // 2.5 m at bottom, -23 cm
DEEP_MAX_M    = 2.83 m   // 2.5 m at bottom, +33 cm
SHALLOW_MIN_M = 0.07 m + FLOAT_HEIGHT = 0.3748 m   // 7 cm at top → bottom
SHALLOW_MAX_M = 0.73 m + FLOAT_HEIGHT = 1.0348 m   // 73 cm at top → bottom
```

The packet output (`PVPHSROV HH:MM:SS XX.X kPa Y.YY meters`) reports `reportedDepth()`, so the score-checking team sees a value directly comparable to `2.5 m` and `0.4 m + 12 in` mission targets.

---

## 4. Buoyancy engine (one-way pump)

The float uses a single DC pump driven by an L298N H-bridge, with PWM speed control on `ENB`.

| Pin | GPIO | Role |
|---|---|---|
| `IN3` | 17 | direction A (HIGH = intake) |
| `IN4` | 18 | direction B (kept LOW — never reversed) |
| `ENB` | 4 | PWM speed (0 = stop, 255 = full) |

**Operating principle:**

- **Pump ON** (intake direction): water flows INTO the float → float gets heavier → sinks.
- **Pump OFF**: residual positive buoyancy lifts the float passively → no reverse pumping needed.

Speed presets:

| Constant | Value | When used |
|---|---|---|
| `MOTOR_SPEED_FULL` | 255 (100%) | DESCEND state — sink fast to save mission time |
| `MOTOR_SPEED_HOLD` | 90 (~35%) | HOLD bands — gentle correction to avoid overshoot |

### HOLD band controller — midpoint bang-bang

In a HOLD state, the float drifts up (passive buoyancy) most of the time. To keep it inside the ±33 cm band:

```c
midpoint = (lo + hi) / 2;
if (depth < midpoint) motor on (HOLD speed);   // sink slightly toward target
else                  motor off;               // let it drift up
```

This produces small oscillations around the midpoint — well inside the ±33 cm band as long as the natural rise rate isn't too aggressive.

### 30-second hold timer

Each HOLD state runs a non-blocking timer:

- Inside band → accumulate hold time
- Exit band → **reset timer to zero** (mission rule: "out of range = restart from zero")
- Reach 30 s → advance to next state

---

## 5. Commands

Single-character keys; identical behavior whether typed at the float's serial monitor or sent wirelessly via the station.

### Mission control

| Key | Wireless | Action |
|---|---|---|
| `S` | `STAR` | Start the 3-profile mission |
| `X` | `ABRT` | Abort — motor off, reset to IDLE |
| `T` | `TEST` | 10-second motor speed ramp self-test |
| `Z` | `ZERO` | Recalibrate depth zero (16-sample average) |
| `P` | `PING` | Connection check (replies `PONG`) |
| `D` | `DUMP` | Stream the entire LittleFS mission log |

### Bench-test commands (fake depth)

These let you exercise the state machine without water by overriding the sensor reading.

| Key | Wireless | fake bottom-depth | Use |
|---|---|---|---|
| `F` | `FAKE` | toggle on/off | enable/disable fake mode |
| `0` | `FK0M` | 0.00 m | surface preset (top above water) |
| `2` | `FK2M` | 2.50 m | deep target |
| `4` | `FK4M` | 0.70 m | shallow target |
| `+` | `FKUP` | +0.10 m | nudge deeper |
| `-` | `FKDN` | -0.10 m | nudge shallower |

A complete dry-run sequence:

```
F  ← fake mode ON
S  ← start mission
2  ← fake depth = 2.50 m → HOLD_DEEP
   (wait 30 s, six packets)
4  ← fake depth = 0.70 m → HOLD_SHALLOW
   (wait 30 s)
0  ← fake depth = 0.00 m → SURFACE → next profile
2 → 30 s wait → 4 → 30 s wait → 0   (profile 2)
2 → 30 s wait → 4 → 30 s wait → 0   (profile 3)
   → MISSION_DONE → STATE = DONE
```

Total bench-test time: ~3 minutes.

---

## 6. Failsafe layers

### Emergency depth (`MAX_DEPTH_M = 3.20 m`)

If reportedDepth ever exceeds 3.20 m (impossible during a clean profile), the firmware:
1. Stops the pump
2. Forces transition to `MS_SURFACE`
3. Lets natural buoyancy bring the float up

### DESCEND watchdog (`STATE_TIMEOUT_MS = 90 s`)

If the float fails to reach the deep band within 90 seconds (pump blocked, sensor stuck, etc.):
1. Stops the pump
2. Forces transition to `MS_SURFACE`

Rise phases (`ASCEND_SHALLOW`, `SURFACE`) intentionally have **no timeout** — natural buoyancy can take several minutes, especially with residual water in the pump. The operator's `X` key is the manual override.

### Boot-safe motor

`setup()` calls `motorStop()` immediately after `pinMode(MOTOR_ENB, OUTPUT)`. This guarantees the pump stays off across resets, even if a glitch left the GPIO momentarily HIGH.

### LittleFS log backup

On every boot, the previous `/mission.log` is renamed to `/mission.log.bak` so an unexpected reset can't erase the last mission's data.

---

## 7. Files involved

| File | Role |
|---|---|
| `float/src/main.cpp` | All firmware logic (state machine, motor, sensor, ESP-NOW, LittleFS) |
| `station/src/main.cpp` | Key-input → 4-byte ESP-NOW command forwarding + RX log |
| `tools/read_and_graph.py` | Pulls `received.log` and renders depth-vs-time with **two traces** (float bottom from packet + computed float top), plus dotted target lines at 2.5 m bottom and 0.4 m top |

For the corresponding constants and helper definitions, search `float/src/main.cpp` for these anchors:
- `#define DEEP_MIN_M` (mission bands)
- `enum MissionState` (state machine)
- `void missionTick(`  (per-tick state evaluation)
- `bool holdControl(`  (midpoint bang-bang)
- `float reportedDepth()` (depth helper — bottom-referenced, used everywhere)

---

## 8. Calibration ritual

Before each deployment:

1. Boot the float on the surface (or in the operator's hand near the surface).
2. Wait for the boot message — `[zero calibration done] offset = X.XXXX m`.
3. If the float is going to be deployed in different water (saltwater, cold), recalibrate with the `Z` command (or BOOT button) once it's floating in the actual water.
4. Verify the first packet shows a sensible depth (`0.30 meters` is the expected value when the sensor is at the top, in air, immediately after calibration).

After recalibration, the mission elapsed-time clock also resets — so calibrate **before** pressing `S`.
