# ESP32-S3 N16R8 — MATE Floats 2026

Autonomous float firmware running on two ESP32-S3 DevKitC-1 N16R8 boards. Built for the **MATE ROV 2026 "Floats Under the Ice"** mission. See [`docs/2026_MATE_Floats_분석.md`](docs/2026_MATE_Floats_분석.md) for full mission details.

- **MCU**: ESP32-S3 N16R8 (16 MB Flash + 8 MB Octal PSRAM)
- **Framework**: Arduino (PlatformIO)
- **Wireless**: ESP-NOW bidirectional unicast (MAC whitelist prevents cross-team interference at competition)
- **Depth sensor**: BlueRobotics MS5837-30BA (I2C 0x76, SDA=GPIO8, SCL=GPIO9)
- **Buoyancy engine**: One-way DC pump on L298N Motor B — IN3=GPIO17, IN4=GPIO18, ENB=GPIO4 (PWM speed)
  - Pump runs ONLY in the descent direction (IN3 HIGH, IN4 LOW): water intake → float sinks
  - When the pump stops, residual positive buoyancy lifts the float passively → no reverse pumping needed
  - HOLD bands use a midpoint-based bang-bang: pump on (gentle) when shallower than midpoint, pump off when deeper
  - ENB jumper must be REMOVED — speed is driven by GPIO4 PWM (0=stop, 255=full)
- **Float geometry**: 13 in (33.02 cm) total — 12 in tube + 0.5 in top cap + 0.5 in bottom cap. The depth sensor sits in the bottom cap **flush with the float's bottom face**, so the sensor reading equals the bottom-of-float depth with zero offset. Bottom-mounting keeps the sensor submerged the longest as the float rises and removes any need to disclose a sensor offset to the judge for the 2.5 m hold.

> **First time setting up?** See [`docs/prerequisites.md`](docs/prerequisites.md) — installs uv, syncs the `tools/` venv, and walks through the two-terminal mission flow.

## Two boards

| Directory  | Role                                                                    |
| ---------- | ----------------------------------------------------------------------- |
| `float/`   | Float board. Depth sensing + packet TX + LittleFS logging + wireless RX |
| `station/` | Ground station board. Packet RX + key-input command TX                  |

## USB connection

Plug the station's USB-C cable into the **`USB`-labeled** port on the DevKitC-1 (not the `UART` side). The `Serial` output you read with the monitor is routed only through the `USB` port.

For the full background on why there are two ports, see the "USB ports — there are two" section in [`CLAUDE.md`](CLAUDE.md).

## Wireless command reference

While the float is on the surface or just after recovery, send commands by **typing keys into the station's serial monitor** (opened via `pio device monitor` from inside the activated `tools/` venv — see prerequisites). The station firmware turns each single character into a 4-byte ESP-NOW command and forwards it.

**Wireless commands (station → float, ESP-NOW):**

| Key | Command sent    | Float behavior                                                                                       | Response (station serial)      |
| --- | --------------- | ---------------------------------------------------------------------------------------------------- | ------------------------------ |
| `S` | `STAR` (=START) | Runs the autonomous mission: 3 vertical profiles (descend → 2.5 m hold 30 s → 40 cm hold 30 s) × 3   | `[RX] MISSION_START` then state logs |
| `X` | `ABRT` (=ABORT) | Stops everything (motor off, mission state machine returned to IDLE)                                 | `[RX] ABORTED`                 |
| `C` | `CALI`          | Manual in-water HOLD-PWM calibration: descends to ≥ 2.27 m, sweeps PWM 60–180 in 7 × 4 s steps, picks the lowest-drift value, saves to `/cali.txt`. (If skipped, the float runs an inline 4-step sweep automatically during the first mission HOLD_DEEP.) Auto-loaded on every boot. | `[RX] CALI_OK pwm=N`           |
| `P` | `PING`          | Connection check reply                                                                               | `[RX] PONG`                    |
| `D` | `DUMP`          | Wirelessly transmits the entire LittleFS mission log line by line (50 ms gap)                        | `[RX] PVPHSROV ...` × N        |

The float serial monitor accepts the same keys (`S`/`X`/`T`/`D`) for direct local control during bench debugging — no station required.

**Local commands (handled by the station itself):**

| Key | Action                           | Notes                                 |
| --- | -------------------------------- | ------------------------------------- |
| `R` | Dumps `received.log` over serial | Sent automatically by the Python tool |
| `E` | Deletes `received.log`           | Cleanup before a new mission          |
| `I` | Prints file / FS usage           | —                                     |
| `H` | Re-prints the help message       | —                                     |

## Typical mission flow (during a run)

Three actors — **Laptop**, **Station** (ground board), and **Float** (submerging board) — cooperate in time order.

![Mission flow diagram](docs/Floats.png)

**Step-by-step summary:**

| #   | Actor           | Action                                                                             | Score    |
| --- | --------------- | ---------------------------------------------------------------------------------- | -------- |
| 1   | Station + Float | Boot both boards (USB / battery). The float begins transmitting 5-second packets immediately. | —        |
| 2   | Laptop          | Open the station serial monitor (terminal 1) and verify packets are coming in.     | —        |
| 3   | Operator        | Place the float on the water (surface). At least one packet must reach the station before the first profile starts. | ② 5 pts  |
| 4   | Laptop          | `S` → station → STAR command → float runs 3 vertical profiles autonomously         | ③④ 50 pts |
| 5   | Float           | No wireless reach while submerged → appends only to its own LittleFS every 5 s     | —        |
| 6   | Operator        | Float surfaces between profiles and at the end → wireless restored                 | —        |
| 7   | Laptop          | `D` → station → DUMP command to float → station saves every packet to its LittleFS | ⑤ 10 pts |
| 8   | Laptop          | In terminal 2: `python read_and_graph.py` → produces `received.png` chart          | ⑥ 10 pts |

**Key-input responsibilities:**

- `S` `X` `T` `C` `P` `D` = the station receives them and forwards via ESP-NOW to the float
- `R` `E` `I` = handled locally by the station (LittleFS dump / erase / info). Either typed by the operator or sent automatically by the Python tool.

See [`docs/prerequisites.md`](docs/prerequisites.md) for the exact two-terminal commands (monitor + graph).

## Directory structure

```text
float/
├── platformio.ini
└── src/main.cpp        # Depth · packets · LittleFS · wireless TX/RX

station/
├── platformio.ini
└── src/main.cpp        # RX + key input → command TX

tools/
├── pyproject.toml      # uv project (platformio, pyserial, matplotlib)
├── uv.lock
└── read_and_graph.py

docs/
├── prerequisites.md
├── Floats.png
└── 2026_MATE_Floats_분석.md
```

## References

- [How it works](docs/how-it-works.md) — Firmware internals: state machine, buoyancy engine, depth coordinates, failsafes ([한글](docs/how-it-works_ko.md))
- [Prerequisites](docs/prerequisites.md) — Install + two-terminal mission flow (macOS / Windows)
- [Mission analysis](docs/2026_MATE_Floats_분석.md) — Scoring / packet format / physical & electrical constraints
- [TODO](TODO.md) — Mission backlog + completed decisions
- [CLAUDE.md](CLAUDE.md) — Dev environment / pitfalls / troubleshooting
- [ESP32-S3 DevKitC-1 official documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [Arduino-ESP32 API reference](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
