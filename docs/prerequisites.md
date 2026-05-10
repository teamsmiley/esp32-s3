# Prerequisites

What to install on the laptop that talks to the boards.

This document assumes the firmware is already on the boards. You only need to type a couple of keys (`Z` / `D`) into the serial monitor and generate the graph after recovery.

The only thing you install yourself is **uv**. PlatformIO is pulled into the `tools/` virtual environment automatically.

## 1. Install uv

**macOS:**

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

**Windows (PowerShell):**

```powershell
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
```

After installing, **open a new terminal** so the updated PATH takes effect, then verify:

```bash
uv --version
```

## 2. Clone the repo with Git

- **macOS**: `xcode-select --install` (Apple's command-line tools include Git)
- **Windows**: [Git for Windows](https://git-scm.com/download/win)

```bash
git clone https://github.com/teamsmiley/esp32-s3.git
cd ESP32-s3
```

## 3. Create and activate the virtual environment

```bash
cd tools
uv sync                       # creates .venv + installs platformio + pyserial + matplotlib

source .venv/bin/activate     # macOS / Linux
# Windows (PowerShell): .venv\Scripts\Activate.ps1
```

Verify:

```bash
pio --version
```

- **shortcut**
  - macOS: `cd tools && source .venv/bin/activate`
  - Windows PowerShell: `cd tools; .venv\Scripts\Activate.ps1`

## Run the mission + generate the graph

Use two terminals. The serial port is held by both the monitor and the graph tool, so keeping them in separate terminals is more reliable.

**Terminal 1 — monitor** (with the venv already activated):

```bash
pio pkg install -g --platform espressif32
pio device monitor --baud 115200
# Z key → recalibrate depth zero
# D key → dump after recovery
# Ctrl+C to exit when the mission is done
```

**Terminal 2 — graph** (open a new terminal):

```bash
cd tools
source .venv/bin/activate    # macOS / Linux
# Windows (PowerShell): .venv\Scripts\Activate.ps1

python read_and_graph.py
```

**Key reference:**

| Key | When to press                            | Effect                                               | Score    |
| --- | ---------------------------------------- | ---------------------------------------------------- | -------- |
| `Z` | Right after placing float on surface     | Recalibrate depth zero (`[RX] ZERO_OK ...` response) | ② 5 pts  |
| `D` | After recovering float (back on surface) | Pulls float's entire mission.log over to the station | ⑤ 10 pts |
| —   | After dump completes, Ctrl+C → graph     | Produces `received.png`                              | ⑥ 10 pts |

---

## Pre-event check (the day before)

With the actual boards plugged in via USB, run the same two-terminal flow above end-to-end once.

**Terminal 1 — monitor:**

```bash
cd tools
source .venv/bin/activate    # macOS / Linux
# Windows (PowerShell): .venv\Scripts\Activate.ps1

pio device monitor --baud 115200
# Confirm [RX] PVPHSROV ... packets stream every 5 seconds
# Press Z once → confirm [RX] ZERO_OK ... response
# Run the mission
# Press D once → confirm [RX] DUMP_OK ... response
# Wait until the chart finishes drawing
# Ctrl+C to exit
```

**Terminal 2 — graph:**

```bash
cd tools
source .venv/bin/activate    # macOS / Linux
# Windows (PowerShell): .venv\Scripts\Activate.ps1

python read_and_graph.py
# Confirm received.log + received.png are produced
```

If everything works here, use the same flow on competition day. If anything breaks, see the OS-specific pitfalls below.

## OS-specific pitfalls

### macOS

- The DevKitC-1 has two USB ports. Plug into the **`USB`-labeled** side. The `UART`-labeled side only shows the ROM bootloader messages (see the "USB ports — there are two" section in `CLAUDE.md` for details).
- If the port isn't detected: run `pio device list` to confirm it shows up. If it doesn't, the cable might be charge-only — swap for a data cable.

### Windows

- The ESP32-S3 native USB appears as a CDC device on Windows 10+ without any driver. If Device Manager shows an "Unknown device", install the [Espressif USB driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) (CP210x or CH343, depending on your board revision).
- Use **Windows Terminal** or **PowerShell**, not legacy `cmd.exe` — they handle the ANSI color codes from `pio device monitor` correctly.
- The USB-C cable must be a **data cable**. Charge-only cables won't work.
