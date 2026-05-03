# ESP32-S3 N16R8 — MATE Floats 2026

Autonomous float firmware running on two ESP32-S3 DevKitC-1 N16R8 boards. Built for the **MATE ROV 2026 "Floats Under the Ice"** mission. See [`docs/2026_MATE_Floats_분석.md`](docs/2026_MATE_Floats_분석.md) for full mission details.

- **MCU**: ESP32-S3 N16R8 (16 MB Flash + 8 MB Octal PSRAM)
- **Framework**: Arduino (PlatformIO)
- **Wireless**: ESP-NOW bidirectional unicast (MAC whitelist prevents cross-team interference at competition)
- **Sensor**: BlueRobotics MS5837-30BA (I2C 0x76, SDA=GPIO8, SCL=GPIO9)

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

| Key | Command sent    | Float behavior                                                                | Response (station serial)      |
| --- | --------------- | ----------------------------------------------------------------------------- | ------------------------------ |
| `D` | `DUMP`          | Wirelessly transmits the entire LittleFS mission log line by line (50 ms gap) | `[RX] PVPHSROV ...` × N        |
| `Z` | `ZERO`          | Recalibrates depth zero (16-sample average) + resets mission timer            | `[RX] ZERO_OK offset=X.XXXX m` |
| `P` | `PING`          | Connection check reply                                                        | `[RX] PONG`                    |
| `S` | `STAR` (=START) | Triggers the autonomous sequence (currently a stub)                           | `[RX] START_STUB`              |

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
| 1   | Station + Float | Boot both boards (USB / battery)                                                   | —        |
| 2   | Laptop          | Open the station serial monitor (terminal 1)                                       | —        |
| 3   | Operator        | Place the float on the water (surface)                                             | —        |
| 4   | Laptop          | `Z` → station → ZERO command to float (zero-point calibration at the surface)      | ② 5 pts  |
| 5   | Operator        | Float dives → runs the mission (2.5 m depth / 40 cm profile)                       | —        |
| 6   | Float           | No wireless reach → appends only to its own LittleFS every 5 s                     | —        |
| 7   | Operator        | Float recovered (back to surface) → wireless restored                              | —        |
| 8   | Laptop          | `D` → station → DUMP command to float → station saves every packet to its LittleFS | ⑤ 10 pts |
| 9   | Laptop          | In terminal 2: `python read_and_graph.py` → produces `received.png` chart          | ⑥ 10 pts |

**Key-input responsibilities:**

- `D` `Z` `P` `S` = the station receives them and forwards via ESP-NOW to the float
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

- [Prerequisites](docs/prerequisites.md) — Install + two-terminal mission flow (macOS / Windows)
- [Mission analysis](docs/2026_MATE_Floats_분석.md) — Scoring / packet format / physical & electrical constraints
- [TODO](TODO.md) — Mission backlog + completed decisions
- [CLAUDE.md](CLAUDE.md) — Dev environment / pitfalls / troubleshooting
- [ESP32-S3 DevKitC-1 official documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [Arduino-ESP32 API reference](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
