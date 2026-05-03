#!/usr/bin/env python3
"""MATE Floats 2026 — station 시리얼 캡처 + 수심-시간 그래프 생성기.

흐름:
  1. station 의 USB 시리얼 포트를 청취
  2. `[RX] PVPHSROV HH:MM:SS XX.X kPa X.XX meters` 라인 파싱
  3. CSV 에 누적 저장 (time_s, time_str, kPa, depth_m, company)
  4. Ctrl+C 시 matplotlib 으로 수심-시간 그래프 PNG 저장

사용 예시:
  실시간 캡처 + 그래프 (포트 자동 탐지):
    uv run capture_and_graph.py

  접속 직후 DUMP 명령 자동 송신 (회수 후 시연용):
    uv run capture_and_graph.py --send-cmd DUMP

  포트 직접 지정 (여러 ESP32-S3 동시 연결 시):
    uv run capture_and_graph.py --port /dev/cu.usbmodemXXXX

  연결된 ESP32-S3 USB 포트만 나열:
    uv run capture_and_graph.py --list-ports

  CSV 에서 그래프만 재생성 (시리얼 사용 안 함):
    uv run capture_and_graph.py --csv mission.csv --png mission.png --replot

주의:
  - station 시리얼은 한 번에 한 프로세스만 점유 가능. `pio device monitor` 와 동시에 못 씀.
  - CSV 는 append 모드. 새 미션 시작 전 기존 파일 삭제 권장.
  - 자동 탐지는 ESP32-S3 네이티브 USB (VID=0x303A) 만 인식. `COM` 라벨 포트(USB-UART)는 다른 VID 라 잡히지 않음 — 이는 의도된 동작 (`Serial` 출력은 `USB` 라벨 쪽으로만 나옴).
"""
import argparse
import csv
import re
import sys
import time
from pathlib import Path

# 패킷 형식: `PVPHSROV HH:MM:SS XX.X kPa X.XX meters`
# station 은 `[RX] ` 프리픽스를 붙임. float 직결 시 프리픽스 없음 — 양쪽 다 매칭.
PACKET_RE = re.compile(
    r"^(?:\[RX\]\s+)?(\w+)\s+(\d{2}):(\d{2}):(\d{2})\s+"
    r"([\d.]+)\s+kPa\s+([\d.]+)\s+meters\s*$"
)

CSV_HEADER = ["time_s", "time_str", "kPa", "depth_m", "company"]

ESP32_S3_VID = 0x303A  # Espressif Systems — 네이티브 USB CDC


def list_esp32s3_ports():
    """연결된 ESP32-S3 네이티브 USB 포트 목록 반환."""
    from serial.tools import list_ports
    return [p for p in list_ports.comports() if p.vid == ESP32_S3_VID]


def autodetect_port() -> str:
    """ESP32-S3 USB 포트가 정확히 1개면 자동 선택, 아니면 안내 후 종료."""
    candidates = list_esp32s3_ports()
    if len(candidates) == 1:
        p = candidates[0]
        print(f"[auto] 포트 자동 선택: {p.device} ({p.description})")
        return p.device
    if not candidates:
        print("[error] ESP32-S3 USB 장치 없음. 케이블이 'USB' 라벨 포트에 꽂혔는지 확인 후 --port 로 직접 지정.")
        sys.exit(1)
    print("[error] ESP32-S3 USB 장치가 여러 개입니다 — --port 로 지정하세요:")
    for p in candidates:
        sn = p.serial_number or "?"
        print(f"  {p.device}  {p.description}  serial={sn}")
    sys.exit(1)


def parse_packet(line: str):
    m = PACKET_RE.match(line.strip())
    if not m:
        return None
    company, hh, mm, ss, kpa, depth = m.groups()
    return {
        "company": company,
        "time_s": int(hh) * 3600 + int(mm) * 60 + int(ss),
        "time_str": f"{hh}:{mm}:{ss}",
        "kPa": float(kpa),
        "depth_m": float(depth),
    }


def write_header_if_needed(path: Path) -> None:
    if path.exists() and path.stat().st_size > 0:
        return
    with path.open("w", newline="") as f:
        csv.writer(f).writerow(CSV_HEADER)


def append_row(path: Path, row: dict) -> None:
    with path.open("a", newline="") as f:
        csv.writer(f).writerow(
            [row["time_s"], row["time_str"], row["kPa"], row["depth_m"], row["company"]]
        )


def load_csv(path: Path):
    times, depths, kpas = [], [], []
    with path.open() as f:
        for row in csv.DictReader(f):
            times.append(int(row["time_s"]))
            depths.append(float(row["depth_m"]))
            kpas.append(float(row["kPa"]))
    return times, depths, kpas


def make_graph(csv_path: Path, png_path: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")  # headless 환경에서도 동작
    import matplotlib.pyplot as plt

    times, depths, _ = load_csv(csv_path)
    if not times:
        print(f"[graph] CSV 비어있음 — 그래프 생략: {csv_path}")
        return

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(times, depths, marker="o", linewidth=1.5, markersize=4)
    ax.set_xlabel("Time (s, since mission start)")
    ax.set_ylabel("Depth (m)")
    ax.set_title("MATE Floats 2026 — Vertical Profile")
    ax.invert_yaxis()  # 깊이는 아래로 갈수록 양수 → 아래로 그리기
    ax.grid(True, alpha=0.3)

    # 미션 규정상 최소 20개 데이터 포인트 — 그래프에 표시
    ax.text(
        0.02, 0.98,
        f"{len(times)} data points",
        transform=ax.transAxes, fontsize=9, va="top",
        bbox=dict(boxstyle="round", facecolor="white", alpha=0.7),
    )

    fig.tight_layout()
    fig.savefig(png_path, dpi=150)
    print(f"[graph] {png_path} 저장 — {len(times)} 포인트")


def capture(port: str, baud: int, csv_path: Path, send_cmd: str | None) -> None:
    import serial

    ser = serial.Serial(port, baud, timeout=1)
    print(f"[capture] {port} @ {baud} baud — 종료: Ctrl+C")
    write_header_if_needed(csv_path)

    if send_cmd:
        time.sleep(1.0)  # ESP-NOW peer 연결 안정화 대기
        ser.write(send_cmd.encode())
        print(f"[capture] 명령 송신: {send_cmd}")

    count = 0
    try:
        while True:
            try:
                raw = ser.readline().decode("utf-8", errors="replace")
            except Exception as e:
                print(f"[capture] read 오류: {e}")
                continue
            if not raw:
                continue
            line = raw.rstrip("\r\n")
            print(line)  # 시리얼 모니터처럼 raw 출력도 같이 보여줌
            packet = parse_packet(line)
            if packet:
                append_row(csv_path, packet)
                count += 1
    except KeyboardInterrupt:
        print(f"\n[capture] 중단 — 이번 세션 {count} 개 패킷 저장 → {csv_path}")
    finally:
        ser.close()


def main() -> None:
    ap = argparse.ArgumentParser(description="MATE Floats station 캡처 + 그래프")
    ap.add_argument("--port", help="station USB 시리얼 포트 (생략 시 자동 탐지)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--csv", default="mission.csv", help="CSV 경로 (append 모드)")
    ap.add_argument("--png", default="mission.png", help="그래프 PNG 출력 경로")
    ap.add_argument("--send-cmd", help="접속 직후 송신할 명령 (예: DUMP)")
    ap.add_argument("--replot", action="store_true", help="CSV → PNG 재생성만")
    ap.add_argument("--list-ports", action="store_true", help="연결된 ESP32-S3 USB 포트 나열 후 종료")
    args = ap.parse_args()

    if args.list_ports:
        ports = list_esp32s3_ports()
        if not ports:
            print("연결된 ESP32-S3 USB 장치 없음.")
            return
        for p in ports:
            sn = p.serial_number or "?"
            print(f"{p.device}  {p.description}  serial={sn}")
        return

    csv_path = Path(args.csv)
    png_path = Path(args.png)

    if args.replot:
        if not csv_path.exists():
            print(f"[error] CSV 없음: {csv_path}")
            sys.exit(1)
        make_graph(csv_path, png_path)
        return

    port = args.port or autodetect_port()
    capture(port, args.baud, csv_path, args.send_cmd)
    make_graph(csv_path, png_path)


if __name__ == "__main__":
    main()
