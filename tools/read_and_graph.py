#!/usr/bin/env python3
"""MATE Floats 2026 — pull the station's received.log over USB and produce a graph PNG.

Flow (no options, run once):
  1. Auto-detect the ESP32-S3 USB port
  2. Open the serial port and send 'R' (triggers the station dump)
  3. Save the lines the station streams back into received.log
  4. Stop automatically when '---- END received.log ----' is seen
  5. Parse the packets and produce received.png (depth-vs-time graph)

Usage:
  uv run read_and_graph.py
"""
import re
import sys
import time
from pathlib import Path

PACKET_RE = re.compile(
    r"^(?:\[RX\]\s+)?(\w+)\s+(\d{2}):(\d{2}):(\d{2})\s+"
    r"([\d.]+)\s+kPa\s+([\d.]+)\s+meters\s*$"
)
END_MARKER = "---- END received.log ----"
ESP32_S3_VID = 0x303A
LOG_PATH = Path("received.log")
PNG_PATH = Path("received.png")


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
    _, hh, mm, ss, kpa, depth = m.groups()
    return int(hh) * 3600 + int(mm) * 60 + int(ss), float(depth)


def make_graph(points: list[tuple[int, float]]) -> None:
    if not points:
        print("[graph] no packets — skipping graph")
        return
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    points = sorted(points, key=lambda p: p[0])
    times, depths = zip(*points)
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(times, depths, marker="o", linewidth=1.5, markersize=4)
    ax.set_xlabel("Time (s, since mission start)")
    ax.set_ylabel("Depth (m)")
    ax.set_title("MATE Floats 2026 — Vertical Profile")
    ax.invert_yaxis()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(PNG_PATH, dpi=150)
    print(f"[graph] saved {PNG_PATH} ({len(points)} packets)")


def main() -> None:
    import serial
    port = autodetect_port()
    ser = serial.Serial(port, 115200, timeout=2)
    time.sleep(1.0)  # let USB-CDC re-enumeration settle
    ser.write(b"R")
    print("[capture] sent 'R' — waiting for dump")

    # Same mission timestamp may arrive twice (live + DUMP), so dedup by time key.
    by_time: dict[int, float] = {}
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
                t, depth = packet
                if t in by_time:
                    duplicates += 1
                else:
                    by_time[t] = depth
            if END_MARKER in line:
                break

    ser.close()
    points = list(by_time.items())
    print(f"[capture] saved {LOG_PATH} — {len(points)} unique packets ({duplicates} duplicate(s) dropped)")
    make_graph(points)


if __name__ == "__main__":
    main()
