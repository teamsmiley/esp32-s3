# How It Works — Float Mission Internals

This document explains how the float firmware autonomously runs the MATE Floats 2026 mission. It covers the state machine, the buoyancy engine, the depth coordinate system, and the failsafe layers.

For the mission spec itself (scoring, packet format, regulations), see [`2026_MATE_Floats_분석.md`](2026_MATE_Floats_분석.md).
For two-board operating procedure, see [`prerequisites.md`](prerequisites.md).

---

## 1. The mission, in one paragraph

When the operator presses `S` at the station, the float runs **3 vertical profiles** without further intervention. Each profile is: descend to **2.5 m** (bottom-of-float reference) and hold for **30 seconds** (±33 cm allowed band), then rise to **40 cm** (top-of-float reference) and hold for **30 seconds**, then return to the surface so the next profile can start. In water the descent and rise legs add real travel time.

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
           │   DESCEND     │  thruster full throttle — sink fast
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

The float is **13 inches (33.02 cm) tall total** — a 12 in tube with a 0.5 in cap on each end. The MS5837 depth sensor lives in the bottom cap, **flush with the float's bottom face**, so the sensor reading equals the bottom-of-float depth directly (zero offset).

```
   ━━━━━ ← top of top cap
  ┃ 0.5 ┃ top cap
   ┌───┐
   │   │
   │ 12│
   │ in│ tube
   │   │
   │   │
   └───┘
  ┃ 0.5 ┃ bottom cap
   ━●━━━ ← bottom of bottom cap = sensor (zero offset)
```

A single helper translates the raw sensor value:

| Function | Returns | Used for |
|---|---|---|
| `reportedDepth()` | sensor reading directly (bottom of float) | All HOLD bands, SURFACE check, packet output, graph |

The mission specifies hold targets in **mixed references** (2.5 m at the bottom, 40 cm at the top), but internally we normalize everything to bottom depth via `reportedDepth()` for consistent band comparisons. SURFACE detection also uses bottom depth (`reportedDepth() ≤ 0.20 m`) — bottom-near mounting keeps the sensor submerged the longest as the float ascends, so the depth signal stays continuous and never saturates at 0 m the way a top-mounted sensor would.

### Mission bands (bottom-referenced, in code)

```c
DEEP_MIN_M    = 2.27 m   // 2.5 m at bottom, -23 cm
DEEP_MAX_M    = 2.83 m   // 2.5 m at bottom, +33 cm
SHALLOW_MIN_M = 0.07 m + FLOAT_HEIGHT = 0.3748 m   // 7 cm at top → bottom
SHALLOW_MAX_M = 0.73 m + FLOAT_HEIGHT = 1.0348 m   // 73 cm at top → bottom
```

The packet output (`PVPHSROV HH:MM:SS XX.X kPa Y.YY meters`) reports `reportedDepth()`, so the score-checking team sees a value directly comparable to `2.5 m` and `0.4 m + 12 in` mission targets.

---

## 4. Propulsion (one-way brushless thruster)

The float uses a single brushless thruster driven by an ESC. The thruster nozzle points UP and expels water upward — the reaction pushes the float **down**.

**ESC model**: Castle Creations Phoenix Edge 60HV (50 V / 60 A). Replaced the previous 12 V 750 GPH bilge pump because the pump was too slow.

| Pin | GPIO | Role |
|---|---|---|
| `ESC_PWM_PIN` | 4 | ESC signal (50 Hz servo PWM, 1000–2000 μs) |

The ESC takes a standard RC servo PWM signal — 50 Hz frame rate, throttle encoded as pulse width:

| Pulse width | Meaning |
|---|---|
| 1000 μs | idle / motor stop |
| 2000 μs | full throttle |

**Operating principle:**

- **Thruster ON** (downward thrust): water expelled upward → reaction force → float sinks.
- **Thruster OFF**: residual positive buoyancy lifts the float passively → no reverse needed (ESC stays in airplane / unidirectional mode).

**Arming sequence**: at boot, `escArm()` attaches ledc (50 Hz / 14-bit — ESP32-S3 LEDC caps each channel at 14 bits) and holds a 1000 μs idle pulse for 2 s. The ESC beeps once → armed. Throttle commands sent before this complete are ignored, so the rest of `setup()` is safe to run.

Speed presets (the internal 0–255 scale maps linearly onto 1000–2000 μs):

| Constant | Value | Mapped pulse | When used |
|---|---|---|---|
| `MOTOR_SPEED_FULL` | 255 (100%) | 2000 μs | DESCEND state — sink fast to save mission time |
| `MOTOR_SPEED_HOLD` | 90 (~35%) | ~1353 μs | HOLD bands — gentle correction to avoid overshoot |

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
| `C` | `CALI` | In-water HOLD-PWM auto-calibration (saves to `/cali.txt`, auto-loaded at boot) |
| `P` | `PING` | Connection check (replies `PONG`) |
| `D` | `DUMP` | Stream the entire LittleFS mission log |

---

## 6. Failsafe layers

### Emergency depth (`MAX_DEPTH_M = 3.20 m`)

If reportedDepth ever exceeds 3.20 m (impossible during a clean profile), the firmware:
1. Stops the thruster
2. Forces transition to `MS_SURFACE`
3. Lets natural buoyancy bring the float up

### DESCEND watchdog (`STATE_TIMEOUT_MS = 90 s`)

If the float fails to reach the deep band within 90 seconds (thruster fouled, sensor stuck, ESC not armed, etc.):
1. Stops the thruster
2. Forces transition to `MS_SURFACE`

Rise phases (`ASCEND_SHALLOW`, `SURFACE`) intentionally have **no timeout** — natural buoyancy can take several minutes. The operator's `X` key is the manual override.

### Boot-safe motor

`setup()` calls `escArm()` before any other initialization. The first thing on the ESC line is a 1000 μs idle pulse held for 2 s — the ESC arms in that idle window, so even if a glitch leaves GPIO4 momentarily HIGH at reset, the ESC won't see a valid throttle until arming completes.

### LittleFS log backup

On every boot, the previous `/mission.log` is renamed to `/mission.log.bak` so an unexpected reset can't erase the last mission's data.

### HOLD-speed calibration

`MOTOR_SPEED_HOLD` is loaded from `/cali.txt` at boot. If the file is missing, malformed, or out of the 1–255 range, the firmware falls back to a hard-coded default (90/255) and sets a `caliPending` flag.

Two trigger paths populate `/cali.txt`:

- **Manual `C` / `CALI`** — explicit calibration before the mission. Float descends from the surface, sweeps 7 PWMs (60–180) for 4 s each, picks the lowest-drift value, saves, surfaces.
- **Automatic inline** — if `caliPending` is true when the float first enters `MS_HOLD_DEEP` during a real mission, a shorter 4-PWM × 3 s sweep (~12 s) runs *before* the 30-second hold timer starts. This handles the "no chance to pre-test in mission water" case automatically.

`X` aborts either path mid-sweep and leaves `/cali.txt` unchanged. Once `caliPending` is cleared (file loaded at boot or sweep completed), subsequent missions skip calibration entirely.

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

## 8. Pre-deployment checklist

Zero-point calibration was removed because the ±33 cm mission tolerance comfortably absorbs the ~±20 cm of atmospheric pressure noise on raw `sensor.depth()`. There is no manual zero step; just confirm the sensor is reporting sensibly:

1. Power on the float and look at the boot output for `[I2C scan ... 0x76 found]` and `[MS5837 init OK]`.
2. With the float in air, the first packet should report a depth close to 0 m (within ~±0.20 m due to weather).
3. Press the BOOT button if you want to reset the mission elapsed-time clock to 0 — this no longer runs any calibration.
4. Place the float in water and press `S` on the station to start the mission.
