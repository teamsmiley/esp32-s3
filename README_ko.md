# ESP32-S3 N16R8 — MATE Floats 2026

ESP32-S3 DevKitC-1 N16R8 두 대로 구성된 자율 플로트 펌웨어. **MATE ROV 2026 "Floats Under the Ice"** 미션 출전용. 미션 상세는 [`docs/2026_MATE_Floats_분석.md`](docs/2026_MATE_Floats_분석.md).

- **MCU**: ESP32-S3 N16R8 (Flash 16 MB + Octal PSRAM 8 MB)
- **프레임워크**: Arduino (PlatformIO)
- **무선**: ESP-NOW 양방향 unicast (MAC 화이트리스트로 대회 충돌 방지)
- **센서**: BlueRobotics MS5837-30BA (I2C 0x76, SDA=GPIO8, SCL=GPIO9)

## 두 보드 / 두 펌웨어

| 디렉토리   | 역할                                                                | 의존성 |
| ---------- | ------------------------------------------------------------------- | ------ |
| `float/`   | 플로트 보드. 깊이 측정 + 패킷 송신 + LittleFS 로깅 + 무선 명령 수신 | MS5837 |
| `station/` | 지상국 보드. 패킷 수신 + 키 입력으로 명령 송신                      | 없음   |

## USB 포트

DevKitC-1 의 USB-C 포트가 두 개입니다.

- **첫 펌웨어 업로드**: `UART` 라벨 쪽 (자동 reset 100% 성공)
- **그 후 일상 사용**: `USB` 라벨 쪽 (펌웨어가 살아있으면 자동 reset 가능, `Serial` 출력이 이쪽으로 라우팅)

자세한 함정·트러블슈팅은 [`CLAUDE.md`](CLAUDE.md) 의 "USB 포트가 두 개" 섹션 참고.

## 빌드 / 업로드 / 모니터

> **처음 셋업하시나요?** [`docs/prerequisites.md`](docs/prerequisites.md) 에 macOS / Windows 설치 방법, 개발자 / 시연자 두 시나리오로 정리되어 있습니다. 1회성 셋업: `cd tools && uv sync`.

터미널마다 한 번씩 `tools/` 가상환경 활성화하면 `pio` 가 그대로 동작:

```bash
cd tools && source .venv/bin/activate    # macOS / Linux
# Windows (PowerShell): cd tools; .venv\Scripts\Activate.ps1
```

활성화 후:

```bash
# 플로트 보드
pio run -d ../float -t upload -t monitor

# 지상국 보드
pio run -d ../station -t upload -t monitor
```

| 명령                                 | 용도               |
| ------------------------------------ | ------------------ |
| `pio run -d ../<board>`              | 빌드만             |
| `pio run -d ../<board> -t upload`    | 빌드 + 업로드      |
| `pio device monitor --baud 115200`   | 시리얼 모니터만    |
| `pio run -d ../<board> -t clean`     | 빌드 결과물 청소   |
| `pio device list`                    | 연결된 시리얼 포트 |

## 무선 명령어 사용법

플로트가 물에 떠 있거나 회수된 직후, **station 에 연결된 시리얼 모니터(아래 Python 도구 권장)에 키 입력** 으로 float 에 명령을 보냅니다. station 펌웨어가 단일 문자를 받아 ESP-NOW 4-byte 명령으로 변환·전달.

**무선 명령 (station → float, ESP-NOW):**

| 키  | 송신 명령       | float 동작                                                  | 응답 (station 시리얼)          |
| --- | --------------- | ----------------------------------------------------------- | ------------------------------ |
| `D` | `DUMP`          | LittleFS 의 미션 로그 전체를 한 줄씩 무선 송신 (50 ms 간격) | `[RX] PVPHSROV ...` × N개      |
| `Z` | `ZERO`          | 깊이 0점 재보정 (16회 평균) + 미션 시간 리셋                | `[RX] ZERO_OK offset=X.XXXX m` |
| `P` | `PING`          | 연결 확인 회신                                              | `[RX] PONG`                    |
| `S` | `STAR` (=START) | 자율 시퀀스 시작 트리거 (현재 stub)                         | `[RX] START_STUB`              |

**로컬 명령 (station 자체 처리):**

| 키  | 동작                       | 비고                    |
| --- | -------------------------- | ----------------------- |
| `R` | `received.log` 시리얼 dump | Python 도구가 자동 송신 |
| `E` | `received.log` 삭제        | 새 미션 전 정리         |
| `I` | 파일/FS 사용량 출력        | —                       |
| `H` | 도움말 다시 출력           | —                       |

float 측은 station MAC 화이트리스트 검사로 다른 팀 명령을 자동 무시. 콜백에선 플래그만 set 하고 무거운 작업 (재보정·dump) 은 `loop()` 에서 처리해 ISR 안전성 확보.

### 일반적인 미션 흐름 (시연 시)

세 행위자 — **Laptop**, **Station** (지상국 보드), **Float** (잠수 보드) — 가 시간 순으로 협력합니다.

![미션 흐름 다이어그램](docs/Floats.png)

**단계별 요약:**

| #   | 행위자          | 동작                                                                                   | 점수   |
| --- | --------------- | -------------------------------------------------------------------------------------- | ------ |
| 1   | Station + Float | 두 보드 부팅 (USB / 배터리)                                                            | —      |
| 2   | Laptop          | `pio device monitor` 로 station 시리얼 열기                                            | —      |
| 3   | 사람            | float 을 물에 띄움 (수면)                                                              | —      |
| 4   | Laptop          | `Z` 키 → station → float 으로 ZERO 명령 (수면 0점 재보정)                              | ② 5점  |
| 5   | 사람            | float 잠수 → 미션 수행 (수심 2.5m / 40cm 프로파일)                                     | —      |
| 6   | Float           | 무선 도달 안 됨 → 자기 LittleFS 에만 5초마다 누적                                      | —      |
| 7   | 사람            | float 회수 (수면) → 무선 복구                                                          | —      |
| 8   | Laptop          | `D` 키 → station → float 으로 DUMP 명령 → station LittleFS 에 모든 패킷 저장           | ⑤ 10점 |
| 9   | Laptop          | 모니터 종료 → `python read_and_graph.py` → `received.png` 그래프 생성                  | ⑥ 10점 |

**키 입력 분담:**

- `D` `Z` `P` `S` = station 이 받아서 ESP-NOW 로 float 에 전달
- `R` `E` `I` = station 로컬 (LittleFS dump/erase/info). 사람이 직접 누르거나 Python 이 자동 송신

## 데이터 읽기 + 그래프 도구 (`tools/`)

```bash
cd tools && source .venv/bin/activate    # 터미널마다 한 번

# 옵션 없음 — station 한 개만 꽂으면 자동 탐지 → R 송신 → received.log + received.png 생성
python read_and_graph.py
```

동작:

- ESP32-S3 USB 포트 자동 탐지 (VID=0x303A)
- `R` 한 글자 송신 → station 의 `received.log` 가 시리얼로 흘러나옴
- 모든 라인을 `received.log` (현재 디렉토리) 에 저장
- `---- END received.log ----` 마커 보면 자동 종료
- 패킷을 파싱해 `received.png` (수심-시간 그래프, Y축 반전) 생성

**주의**: station 시리얼은 한 번에 한 프로세스만 점유 가능. 이 도구를 띄우기 전에 `pio device monitor` 가 떠있으면 충돌하므로 종료 필요 (`pkill -f "pio.*monitor"`).

## 디렉토리 구조

```text
.
├── float/
│   ├── platformio.ini      # 보드 + MS5837 라이브러리
│   └── src/main.cpp        # 깊이·패킷·LittleFS·무선 송수신
├── station/
│   ├── platformio.ini      # 보드 (라이브러리 없음)
│   └── src/main.cpp        # 수신 + 키 입력 → 명령 송신
├── tools/
│   ├── pyproject.toml      # uv 프로젝트 (platformio, pyserial, matplotlib)
│   ├── uv.lock
│   └── read_and_graph.py
├── docs/
│   ├── prerequisites.md
│   └── 2026_MATE_Floats_분석.md
├── examples/               # 과거 학습 단계 코드 (LED, 버튼, 토글)
├── CLAUDE.md               # 개발 가이드 (보드 설정, 함정, 트러블슈팅)
├── TODO.md                 # 미션 진행 항목 + 완료 기록
└── README.md
```

## 참고 문서

- [사전 요구사항](docs/prerequisites.md) — macOS / Windows 설치 방법, 개발자용 / 대회 당일용 셋업
- [미션 분석](docs/2026_MATE_Floats_분석.md) — 점수표 / 패킷 형식 / 물리·전기 제약
- [TODO](TODO.md) — 미션 진행 항목 + 완료된 결정사항
- [CLAUDE.md](CLAUDE.md) — 개발 환경 / 함정 / 트러블슈팅
- [ESP32-S3 DevKitC-1 공식 문서](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [Arduino-ESP32 API 레퍼런스](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
