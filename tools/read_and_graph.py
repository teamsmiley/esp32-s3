#!/usr/bin/env python3
"""MATE Floats 2026 — pull the station's received.log over USB and produce graph PNGs.

Flow (no options, run once):
  1. Auto-detect the ESP32-S3 USB port
  2. Open the serial port and send 'R' (triggers the station dump)
  3. Save the lines the station streams back into received.log
  4. Stop automatically when '---- END received.log ----' is seen
  5. Parse the packets and produce received.png (depth-vs-time)
     and received_pwm.png (pwm-vs-time, if any packet carries a pwm field)

Usage:
  uv run read_and_graph.py
"""
import re
import sys
import time
from pathlib import Path

PACKET_RE = re.compile(
    r"^(?:\[RX\]\s+)?(\w+)\s+(\d{2}):(\d{2}):(\d{2})\s+"
    r"([\d.]+)\s+kPa\s+([\d.]+)\s+meters(?:\s+pwm=(\d+))?\s*$"
)
END_MARKER = "---- END received.log ----"
ESP32_S3_VID = 0x303A
LOG_PATH = Path("received.log")
DEPTH_PNG_PATH = Path("received_depth.png")
PWM_PNG_PATH = Path("received_pwm.png")

# Float geometry — packet depth is the float BOTTOM. Top = bottom - FLOAT_HEIGHT_M.
# Total length is 13 in (12" tube + two 0.5" end caps).
# Mission targets: BOTTOM at 2.5 m for the deep hold, TOP at 0.4 m for the shallow hold.
FLOAT_HEIGHT_M = 0.3302   # 13 in
DEEP_TARGET_M  = 2.50     # bottom-referenced
SHALLOW_TARGET_TOP_M = 0.40   # top-referenced


def autodetect_port() -> str:
    from serial.tools import list_ports
    candidates = [p for p in list_ports.comports() if p.vid == ESP32_S3_VID]
    if len(candidates) == 1:
        print(f"[port] {candidates[0].device}")
        return candidates[0].device
    if not candidates:
        sys.exit("[error] no ESP32-S3 USB device found. Make sure the cable is in the 'USB'-labeled port.")
    sys.exit(f"[error] multiple ESP32-S3 USB devices: {[p.device for p in candidates]}")


def parse_packet(line: str):
    m = PACKET_RE.match(line.strip())
    if not m:
        return None
    _, hh, mm, ss, kpa, depth, pwm = m.groups()
    t = int(hh) * 3600 + int(mm) * 60 + int(ss)
    return t, float(depth), int(pwm) if pwm is not None else None


def make_depth_graph(points: list[tuple[int, float]]) -> None:
    if not points:
        print("[graph] no depth packets — skipping depth graph")
        return
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    points = sorted(points, key=lambda p: p[0])
    times, bottoms = zip(*points)
    tops = [b - FLOAT_HEIGHT_M for b in bottoms]

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(times, bottoms, marker="o", linewidth=1.5, markersize=4,
            color="tab:blue", label="Float bottom (packet value)")
    ax.plot(times, tops, marker="s", linewidth=1.5, markersize=3, linestyle="--",
            color="tab:orange", label=f"Float top (bottom − {FLOAT_HEIGHT_M:.4f} m)")

    # Mission target lines so the judge can spot the two holds at a glance.
    ax.axhline(DEEP_TARGET_M, color="tab:blue", linestyle=":", alpha=0.5,
               label=f"Deep target: bottom = {DEEP_TARGET_M} m")
    ax.axhline(SHALLOW_TARGET_TOP_M, color="tab:orange", linestyle=":", alpha=0.5,
               label=f"Shallow target: top = {SHALLOW_TARGET_TOP_M} m")

    ax.set_xlabel("Time (s, since mission start)")
    ax.set_ylabel("Depth (m)")
    ax.set_title("MATE Floats 2026 — Vertical Profile (bottom & top of float)")
    ax.invert_yaxis()
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=9)
    fig.tight_layout()
    fig.savefig(DEPTH_PNG_PATH, dpi=150)
    print(f"[graph] saved {DEPTH_PNG_PATH} ({len(points)} packets, bottom + top traces)")


def make_pwm_graph(points: list[tuple[int, int]]) -> None:
    if not points:
        print("[graph] no pwm packets — skipping pwm graph")
        return
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    points = sorted(points, key=lambda p: p[0])
    times, pwms = zip(*points)

    fig, ax = plt.subplots(figsize=(10, 4))
    # Step plot: PWM holds its value between packets, so 'post' steps reflect reality.
    ax.step(times, pwms, where="post", linewidth=1.5, color="tab:green",
            label="Motor PWM (0–255)")
    ax.fill_between(times, pwms, step="post", color="tab:green", alpha=0.15)

    ax.set_xlabel("Time (s, since mission start)")
    ax.set_ylabel("PWM (0–255)")
    ax.set_title("MATE Floats 2026 — Motor PWM over Time")
    ax.set_ylim(0, 260)
    ax.invert_yaxis()   # 255 (full power) at the bottom — matches depth graph orientation
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=9)
    fig.tight_layout()
    fig.savefig(PWM_PNG_PATH, dpi=150)
    print(f"[graph] saved {PWM_PNG_PATH} ({len(points)} pwm samples)")


def main() -> None:
    import serial
    port = autodetect_port()
    ser = serial.Serial(port, 115200, timeout=2)
    time.sleep(1.0)  # let USB-CDC re-enumeration settle
    ser.write(b"R")
    print("[capture] sent 'R' — waiting for dump")

    # Same mission timestamp may arrive twice (live + DUMP), so dedup by time key.
    depth_by_time: dict[int, float] = {}
    pwm_by_time: dict[int, int] = {}
    duplicates = 0
    with LOG_PATH.open("w") as logf:
        while True:
            raw = ser.readline().decode("utf-8", errors="replace")
            if not raw:
                continue
            line = raw.rstrip("\r\n")
            print(line)
            logf.write(line + "\n")
            packet = parse_packet(line)
            if packet:
                t, depth, pwm = packet
                if t in depth_by_time:
                    duplicates += 1
                else:
                    depth_by_time[t] = depth
                    if pwm is not None:
                        pwm_by_time[t] = pwm
            if END_MARKER in line:
                break

    ser.close()
    depth_points = list(depth_by_time.items())
    pwm_points = list(pwm_by_time.items())
    print(f"[capture] saved {LOG_PATH} — {len(depth_points)} unique packets "
          f"({duplicates} duplicate(s) dropped, {len(pwm_points)} with pwm)")
    make_depth_graph(depth_points)
    make_pwm_graph(pwm_points)


if __name__ == "__main__":
    main()
