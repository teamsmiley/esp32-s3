# ESP32-S3 N16R8 — MATE Floats 2026

ESP32-S3 DevKitC-1 N16R8 두 대로 구성된 자율 플로트 펌웨어. **MATE ROV 2026 "Floats Under the Ice"** 미션 출전용. 미션 상세는 [`docs/2026_MATE_Floats_분석.md`](docs/2026_MATE_Floats_분석.md).

- **MCU**: ESP32-S3 N16R8 (Flash 16 MB + Octal PSRAM 8 MB)
- **프레임워크**: Arduino (PlatformIO)
- **무선**: ESP-NOW 양방향 unicast (MAC 화이트리스트로 대회 충돌 방지)
- **센서**: BlueRobotics MS5837-30BA (I2C 0x76, SDA=GPIO8, SCL=GPIO9)

## 두 보드 / 두 펌웨어

| 디렉토리 | 역할 | 의존성 |
| -------- | ---- | ------ |
| `float/` | 플로트 보드. 깊이 측정 + 패킷 송신 + LittleFS 로깅 + 무선 명령 수신 | MS5837 |
| `station/` | 지상국 보드. 패킷 수신 + 키 입력으로 명령 송신 | 없음 |

## USB 포트

DevKitC-1 의 USB-C 포트가 두 개입니다.

- **첫 펌웨어 업로드**: `UART` 라벨 쪽 (자동 reset 100% 성공)
- **그 후 일상 사용**: `USB` 라벨 쪽 (펌웨어가 살아있으면 자동 reset 가능, `Serial` 출력이 이쪽으로 라우팅)

자세한 함정·트러블슈팅은 [`CLAUDE.md`](CLAUDE.md) 의 "USB 포트가 두 개" 섹션 참고.

## 빌드 / 업로드 / 모니터

각 보드는 자기 디렉토리 안에서:

```bash
# 플로트 보드
cd float && pio run -t upload -t monitor

# 지상국 보드
cd station && pio run -t upload -t monitor
```

두 보드를 동시에 같은 컴퓨터에 연결했다면 포트 명시 필요:

```bash
pio device list   # 두 포트 확인
cd float   && pio run -t upload -t monitor --upload-port /dev/cu.usbmodemAAAA
cd station && pio run -t upload -t monitor --upload-port /dev/cu.usbmodemBBBB
```

| 명령                 | 용도                |
| -------------------- | ------------------- |
| `pio run`            | 빌드만              |
| `pio run -t upload`  | 빌드 + 업로드       |
| `pio device monitor` | 시리얼 모니터만     |
| `pio run -t clean`   | 빌드 결과물 청소    |
| `pio device list`    | 연결된 시리얼 포트  |

## 무선 명령어 사용법

플로트가 물에 떠 있거나 회수된 직후, **station 시리얼 모니터에 키 입력** 으로 float 에 명령을 보냅니다. 모든 명령은 4 byte 텍스트로 통일.

| 키 | 송신 명령 | float 동작 | 응답 (station 시리얼) |
| -- | --------- | ---------- | ---------------------- |
| `D` | `DUMP` | LittleFS 의 미션 로그 전체를 한 줄씩 무선 송신 (50 ms 간격) | `[RX] PVPHSROV ...` × N개 |
| `Z` | `ZERO` | 깊이 0점 재보정 (16회 평균) + 미션 시간 리셋 | `[RX] ZERO_OK offset=X.XXXX m` |
| `P` | `PING` | 연결 확인 회신 | `[RX] PONG` |
| `S` | `STAR` (=START) | 자율 시퀀스 시작 트리거 (현재 stub) | `[RX] START_STUB` |
| `H` | (로컬) | 도움말 다시 출력 | — |

float 측은 station MAC 화이트리스트 검사로 다른 팀 명령을 자동 무시. 콜백에선 플래그만 set 하고 무거운 작업 (재보정·dump) 은 `loop()` 에서 처리해 ISR 안전성 확보.

### 일반적인 미션 흐름 (시연 시)

1. 플로트를 물에 띄움 → `pio device monitor` 로 station 시리얼 보면 패킷이 5초마다 들어옴
2. 배포 직전 station 모니터에서 `Z` (ZERO) → 수면을 정확히 0점으로
3. 배포 직전 마지막 패킷 1회 = **미션 점수 ② (5점)**
4. 플로트 잠수 → 무선 도달 안 됨. 디스크에 5초마다 누적 (LittleFS)
5. 플로트 회수 → 다시 무선 도달
6. **station 모니터 닫고** Python 도구로 dump 캡처 = **미션 점수 ⑤ (10점)**:
   ```bash
   cd tools && uv run capture_and_graph.py --send-cmd DUMP
   ```
   도구가 자동으로 DUMP 명령 송신 → 패킷을 `mission.csv` 로 저장
7. dump 끝나면 (패킷 더 안 들어오면) `Ctrl+C` → `mission.png` 자동 생성 = **미션 점수 ⑥ (10점)**

**주의**: 시연 중 station 모니터에서 손으로 `D` 누르지 마세요 — 패킷이 화면에만 출력되고 CSV 에 안 남습니다. 손으로 누르는 건 개발 디버깅용. 시연 본번은 항상 Python 의 `--send-cmd DUMP` 경유.

(만약 실수로 손으로 D 를 먼저 눌렀어도 데이터는 float LittleFS 에 그대로 살아있으니 모니터 닫고 Python 도구로 다시 dump 하면 됨.)

## 데이터 캡처 + 그래프 도구 (`tools/`)

회수 후 station 에 들어온 패킷을 컴퓨터에서 받아 **CSV 누적 + 수심-시간 그래프 PNG** 까지 한 번에 만드는 Python 도구. 미션 점수 ⑥ (수심-시간 그래프) 의 토대.

```bash
cd tools

# 첫 실행만: 의존성 sync (uv 가 .venv 자동 생성 + 설치)
uv sync

# 시연 본 흐름: station 한 개만 꽂힌 상태에서 자동 탐지 + DUMP 자동 송신
uv run capture_and_graph.py --send-cmd DUMP
# Ctrl+C → mission.csv 저장 + mission.png 그래프 생성

# 양쪽 보드 다 꽂혔을 때: --list-ports 로 MAC 확인 후 명시
uv run capture_and_graph.py --list-ports
uv run capture_and_graph.py --port /dev/cu.usbmodemXXXX --send-cmd DUMP

# 시리얼 없이 기존 CSV → PNG 재생성
uv run capture_and_graph.py --csv mission.csv --png mission.png --replot
```

| 옵션 | 기본값 | 용도 |
| ---- | ------ | ---- |
| `--port` | (자동 탐지) | ESP32-S3 USB (VID=0x303A) 1 개면 자동 선택 |
| `--csv` | `mission.csv` | append 모드 — 새 미션 전 기존 파일 삭제 권장 |
| `--png` | `mission.png` | Y축 반전(깊이↓), 데이터 포인트 수 라벨 |
| `--send-cmd` | (없음) | 접속 직후 송신할 명령 (`DUMP`, `PING` 등) |
| `--replot` | `false` | 시리얼 사용 안 하고 CSV → PNG 만 |
| `--list-ports` | — | ESP32-S3 USB 포트 나열 (serial 필드에 MAC 노출) |

**주의**: station 시리얼은 한 번에 한 프로세스만 점유 가능 — `pio device monitor` 와 동시에 실행하면 충돌. 시연 전에는 모니터 닫고 이 도구만 띄우세요.

## 디렉토리 구조

```
.
├── float/
│   ├── platformio.ini      # 보드 + MS5837 라이브러리
│   └── src/main.cpp        # 깊이·패킷·LittleFS·무선 송수신
├── station/
│   ├── platformio.ini      # 보드 (라이브러리 없음)
│   └── src/main.cpp        # 수신 + 키 입력 → 명령 송신
├── tools/
│   ├── pyproject.toml      # uv 프로젝트 (pyserial, matplotlib)
│   ├── uv.lock
│   └── capture_and_graph.py
├── docs/
│   └── 2026_MATE_Floats_분석.md
├── examples/               # 과거 학습 단계 코드 (LED, 버튼, 토글)
├── CLAUDE.md               # 개발 가이드 (보드 설정, 함정, 트러블슈팅)
├── TODO.md                 # 미션 진행 항목 + 완료 기록
└── README.md
```

## 참고 문서

- [미션 분석](docs/2026_MATE_Floats_분석.md) — 점수표 / 패킷 형식 / 물리·전기 제약
- [TODO](TODO.md) — 미션 진행 항목 + 완료된 결정사항
- [CLAUDE.md](CLAUDE.md) — 개발 환경 / 함정 / 트러블슈팅
- [ESP32-S3 DevKitC-1 공식 문서](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [Arduino-ESP32 API 레퍼런스](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
