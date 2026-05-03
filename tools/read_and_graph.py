#!/usr/bin/env python3
"""MATE Floats 2026 — station 의 received.log 를 USB 로 읽어와 그래프 PNG 생성.

흐름 (옵션 없이 한 번 실행):
  1. ESP32-S3 USB 포트 자동 탐지
  2. 시리얼 열고 'R' 송신 (station 의 dump 트리거)
  3. station 이 흘려보내는 라인들을 received.log 에 저장
  4. '---- END received.log ----' 만나면 자동 종료
  5. 패킷을 파싱해 received.png (수심-시간 그래프) 생성

사용:
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
        sys.exit("[error] ESP32-S3 USB 장치 없음. 케이블이 'USB' 라벨 포트인지 확인.")
    sys.exit(f"[error] ESP32-S3 USB 장치 여러 개: {[p.device for p in candidates]}")


def parse_packet(line: str):
    m = PACKET_RE.match(line.strip())
    if not m:
        return None
    _, hh, mm, ss, kpa, depth = m.groups()
    return int(hh) * 3600 + int(mm) * 60 + int(ss), float(depth)


def make_graph(points: list[tuple[int, float]]) -> None:
    if not points:
        print("[graph] 패킷 0개 — 그래프 생략")
        return
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
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
    print(f"[graph] {PNG_PATH} 저장 ({len(points)} 패킷)")


def main() -> None:
    import serial
    port = autodetect_port()
    ser = serial.Serial(port, 115200, timeout=2)
    time.sleep(1.0)  # USB-CDC 재열거 안정화
    ser.write(b"R")
    print("[capture] 'R' 송신 — dump 대기")

    points: list[tuple[int, float]] = []
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
                points.append(packet)
            if END_MARKER in line:
                break

    ser.close()
    print(f"[capture] {LOG_PATH} 저장 — {len(points)} 패킷")
    make_graph(points)


if __name__ == "__main__":
    main()
