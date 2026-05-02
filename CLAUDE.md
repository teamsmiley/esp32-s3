# CLAUDE.md

이 파일은 Claude Code (claude.ai/code) 가 이 저장소에서 작업할 때 참고하는 가이드입니다.

## 프로젝트 개요

**MATE ROV 2026 "Floats Under the Ice" 미션 출전용 자율 플로트 펌웨어**.

하드웨어는 **ESP32-S3 DevKitC-1 N16R8** + MS5837-30BA 압력/깊이 센서 기반. 미션 상세 분석은 `docs/2026_MATE_Floats_분석.md` 참고.

## 미션 컨텍스트

- **목표 동작**: 자율 수직 프로파일 (수면 → 수심 2.5m 하강·30초 유지 → 수심 40cm 상승·30초 유지) × 2회 반복
- **정확도 요건**: ±33cm 범위 안에서 30초 유지 (이탈 시 처음부터 재측정)
- **데이터 패킷**: 5초 간격 (0/5/10/15/20/25/30초 = 7개 연속). 회사번호 / 시간 / 압력(kPa) / 수심(m) 포함
- **통신**: 배포 직전 1회 + 회수 후 전체 데이터 무선 전송. 테더·에어라인 불가 (완전 자율)
- **패널티**: 수면 돌파 또는 얼음 접촉 시 해당 프로파일 -5점
- **점수**: 총 70점 (프로파일 50점 + 통신 15점 + DOC-004 5점 등 — 상세 표는 분석 문서)

## 물리·전기 제약

- **크기**: 전체 높이 1m 미만 (안테나 포함), 지름 18cm 미만
- **회수 장치 (2026 신규)**: 5cm 폭 / 5cm 돌출 이상 (U-볼트, 로프 루프 등). 이 장치만 18cm 초과 허용
- **부력 방식**: 모터로 공기/액체 이동만 인정. 프로펠러·워터젯 추진 **불가**
- **배터리**: NiMH 또는 AGM **만** 허용. LiPo / 알카라인 **불가**. 최대 12VDC / 5A
- **퓨즈**: 단일 퓨즈, 배터리 양극 5cm 이내, ATO/MINI/카트리지, 32VDC 정격 이상
- **압력 해방**: 2.5cm 이상 직경 구멍 또는 팝오프 엔드캡 필수 (밸브·호스 클램프 불가)

## 데이터 패킷 형식

미션 규정상 정의된 형식. 모든 통신·로깅 코드에서 일관되게 사용.

```text
<회사번호> <시간> <압력 kPa> <수심 m>
예: EX01 1:51:42 UTC 9.8 kPa 1.00 meters
```

- 시간: UTC / 로컬 / 플로트 시작 후 경과 시간 중 하나 (어느 것을 쓸지 선택 후 고정)
- 압력 또는 수심 중 적어도 하나 (가능하면 둘 다)

## 하드웨어 인벤토리 / 핀 매핑

검증 끝난 항목과 향후 추가될 항목.

| 컴포넌트                       | 상태  | 핀 / 주소                                    |
| ------------------------------ | ----- | -------------------------------------------- |
| MCU ESP32-S3 N16R8             | 검증  | —                                            |
| 깊이 센서 MS5837-30BA          | 검증  | I2C SDA=GPIO8, SCL=GPIO9, addr=0x76          |
| 온보드 RGB LED (상태 표시용)   | 검증  | GPIO48 (`neopixelWrite`)                     |
| BOOT 버튼                      | 검증  | GPIO0 (`INPUT_PULLUP`, debounce 필요)        |
| 부력 엔진 (모터 / 시린지 펌프) | TBD   | —                                            |
| 무선 통신 (WiFi / LoRa / 433M) | TBD   | —                                            |
| 깊이 제어 PID                  | TBD   | (소프트웨어)                                 |
| 데이터 로깅 (LittleFS)         | TBD   | (Flash 16MB 중 일부)                         |
| 배터리 + 단일 퓨즈             | TBD   | —                                            |

신규 컴포넌트를 추가할 때는 이 표에 핀과 검증 상태를 기록.

## 보드 고유 설정 (중요)

PlatformIO 의 기본 `esp32-s3-devkitc-1` 보드 정의는 **N8 변형** (8 MB Flash, PSRAM 없음) 을 가정합니다. 이 보드는 **N16R8** (16 MB Flash + 8 MB Octal PSRAM) 이라서 `platformio.ini` 에서 기본값을 덮어씁니다. **다음 설정은 절대 단순화하거나 삭제하면 안 됩니다** — 빼면 메모리 맵이 조용히 잘못된 상태로 회귀합니다.

- `board_build.arduino.memory_type = qio_opi` — **가장 중요한 설정**. "R8" PSRAM 은 Quad 가 아니라 Octal (OPI) 입니다. `qio_opi` 가 빠지면 PSRAM 자체가 인식되지 않습니다.
- `board_build.psram_type = opi` — 같은 이유
- `board_build.partitions = default_16MB.csv` — 빠지면 Flash 가 8 MB 만 사용 가능
- `-DARDUINO_USB_MODE=1` + `-DARDUINO_USB_CDC_ON_BOOT=1` — `Serial` 출력을 UART0 가 아니라 네이티브 USB 포트 (HWCDC) 로 라우팅

검증용 코드 (새로 진단 코드를 만들 때 기대값):

```cpp
ESP.getFlashChipSize()  // 16 * 1024 * 1024
ESP.getPsramSize()      // ~8 MB (오버헤드 때문에 정수 나눗셈 시 7 로 나옴 — 실제 확인은 Free PSRAM ≈ 8189 KB)
```

## USB 포트가 두 개 — 흔한 함정

DevKitC-1 에는 **USB-C 포트가 두 개** 있습니다: 보드에 **`COM`** 라벨이 적힌 포트 (온보드 USB-UART 변환 칩 → ESP32-S3 의 UART0) 와 **`USB`** 라벨 포트 (네이티브 USB → ESP32-S3 D+/D-). macOS 에서는 각각 다른 `/dev/cu.usbmodem*` 으로 잡힙니다.

현재 `ARDUINO_USB_CDC_ON_BOOT=1` 설정 때문에, `Serial` 은 **`USB` 라벨 포트로만 출력**합니다. `COM` 포트에 케이블을 꽂으면 ROM 부트 메시지는 보이지만 스케치의 `Serial.print(...)` 는 **하나도 보이지 않습니다** — 이게 보드가 멈춘 것처럼 보여서 헷갈리기 쉽습니다. "출력이 안 나옴" 이슈를 디버깅할 때 가장 먼저 확인할 것은 **케이블이 어느 포트에 꽂혔는가** 입니다.

양쪽 모두로 출력하고 싶으면, `Serial` 과 함께 `Serial0` (항상 UART0, 즉 `COM` 포트) 도 같이 사용하면 됩니다.

## 자주 쓰는 명령어

사용자의 `~/.zshrc` 에 `alias pio="$HOME/.platformio/penv/bin/pio"` 가 등록되어 있습니다. 사용자에게 명령어를 안내할 때는 전체 경로 대신 `pio` 를 사용하세요.

| 명령어                         | 용도                                                                              |
| ------------------------------ | --------------------------------------------------------------------------------- |
| `pio run -t upload -t monitor` | **메인 개발 루프** — 빌드, 업로드, 시리얼 모니터까지 한 번에 (포트 잠금 자동 처리) |
| `pio run`                      | 빌드만                                                                            |
| `pio device monitor`           | 시리얼 모니터 열기 (115200 baud, `platformio.ini` 에 설정)                        |
| `pio device list`              | 사용 가능한 시리얼 포트 목록                                                      |
| `pio run -t clean`             | 빌드 결과물 청소 (Flash/PSRAM 설정 변경 후 권장)                                  |

CI 나 스크립트에서 포트가 다를 때는 `--upload-port /dev/cu.usbmodemXXXXX` 를 명시적으로 전달하세요. 포트 ID 는 USB 포트마다 다릅니다.

## 시리얼 출력 타이밍

펌웨어 부팅 후 USB-CDC 가 재열거되기 때문에, `setup()` 첫 1초 정도의 `Serial.println()` 출력은 보통 유실됩니다. 모니터는 중간에 다시 붙습니다 (`Disconnected ... Reconnecting`). 다음 두 가지 방법이 효과적입니다:

1. `setup()` 맨 위에 `delay(3000)` — 단순한 블라인드 대기
2. `loop()` 에서 주기적으로 다시 출력 — 모니터를 언제 열든 상태를 다시 확인 가능

"부팅 출력이 안 보임" 문제를 디버깅할 때, 모니터를 열어둔 상태에서 보드의 RST 버튼을 누르라고 안내하면 가장 빠르게 `setup()` 출력을 확인할 수 있습니다.

## 마일스톤 완료 시 백업 절차

검증된 동작 (센서 동작, 부력 엔진 제어, 통신 등) 을 미션 본 코드에 통합하기 전에는 **반드시** 다음을 수행합니다.

1. **`src/main.cpp` 를 `examples/` 에 보존**
   - 파일명 규칙: `examples/<짧은-설명>.cpp` (예: `examples/depth-sensor-readout.cpp`)
   - 이유: `src/main.cpp` 는 다음 작업에서 덮어써질 수 있음. examples 는 검증된 동작의 최소 재현 코드 (sanity check 용)
   - `examples/` 는 PlatformIO 빌드 대상이 아니므로 컴파일 충돌 없음

2. **git 커밋**
   - 커밋 메시지는 미션 진척 기준으로 (예: `깊이 센서 읽기 검증`, `부력 엔진 PWM 제어 추가`)
   - 커밋은 사용자가 명시적으로 요청할 때만. 자동으로 하지 말 것

## 트러블슈팅: 어디서 답을 찾을까

문제가 어느 층에서 발생했는지에 따라 검색 위치가 다릅니다.

| 증상                              | 1순위 검색 위치                         |
| --------------------------------- | --------------------------------------- |
| `Serial.print` 동작 이상          | GitHub `espressif/arduino-esp32` Issues |
| WiFi / BLE / PSRAM 동작 문제      | Espressif 공식 문서 (ESP-IDF)           |
| GPIO / 핀 동작 문제               | ESP32-S3 데이터시트, 보드 매뉴얼        |
| 펌웨어 업로드 실패, 빌드 에러     | PlatformIO 문서, `esptool` GitHub       |
| USB 안 잡힘, 전원 문제            | DevKitC-1 레퍼런스, 보드 판매자         |
| Arduino API 일반 (digitalWrite 등)| arduino.stackexchange.com               |
| MS5837 / 압력 센서                | BlueRobotics MS5837 GitHub              |
| MATE 미션 규정 해석               | `docs/2026_MATE_Floats_분석.md`         |

**검색 키워드 패턴**: `ESP32-S3 [증상] arduino` 형태로 검색하면 옛날 ESP32 정보와 섞이지 않습니다. 항상 **`ESP32-S3`** (하이픈 포함) 명시.

**핵심 자료 출처:**

- `espressif/arduino-esp32` GitHub — Arduino 호환 레이어 (Espressif 가 직접 관리)
- docs.espressif.com/projects/esp-idf — ESP-IDF 공식 문서
- docs.espressif.com/projects/arduino-esp32 — Arduino-ESP32 공식 문서
- docs.platformio.org — PlatformIO 공식 문서
- esp32.com — Espressif 커뮤니티 포럼

**참고**: ESP32 시리즈는 90% 이상 답이 Espressif 쪽에 있습니다. Arduino 회사 자체는 표준만 정하고 ESP32 지원은 거의 안 합니다.

## 디렉토리 구조

- `src/main.cpp` — 현재 작업 중인 단일 파일 스케치 (검증 끝난 후 `examples/` 로 백업)
- `platformio.ini` — 보드 설정 (기본값으로 무작정 재생성하지 말 것)
- `docs/2026_MATE_Floats_분석.md` — 미션 규정 분석 (점수표, 패킷 형식, 물리·전기 제약)
- `examples/` — 검증된 동작의 최소 재현 코드 (LED, 버튼, 센서 등). PlatformIO 빌드 대상 아님
- `include/`, `lib/`, `test/` — 아직 사용 안 함 (PlatformIO 스캐폴딩, 각 폴더의 `README` 참고)
