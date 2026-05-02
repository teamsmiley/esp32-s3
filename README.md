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

1. 플로트를 물에 띄움 → station 시리얼에 패킷이 5초마다 들어오기 시작
2. 배포 직전 station 에서 `Z` (ZERO) → 수면을 정확히 0점으로
3. 배포 직전 마지막 패킷 1회 = **미션 점수 ② (5점)**
4. 플로트 잠수 → 무선 도달 안 됨. 디스크에 5초마다 누적 (LittleFS)
5. 플로트 회수 → 다시 무선 도달
6. station 에서 `D` (DUMP) → 디스크 전체 송신 = **미션 점수 ⑤ (10점)**
7. 받은 패킷을 그래프로 = **미션 점수 ⑥ (10점)**

## 디렉토리 구조

```
.
├── float/
│   ├── platformio.ini      # 보드 + MS5837 라이브러리
│   └── src/main.cpp        # 깊이·패킷·LittleFS·무선 송수신
├── station/
│   ├── platformio.ini      # 보드 (라이브러리 없음)
│   └── src/main.cpp        # 수신 + 키 입력 → 명령 송신
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
