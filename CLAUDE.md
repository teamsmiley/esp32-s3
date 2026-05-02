# CLAUDE.md

이 파일은 Claude Code (claude.ai/code) 가 이 저장소에서 작업할 때 참고하는 가이드입니다.

## 프로젝트 개요

**ESP32-S3 DevKitC-1 N16R8** 보드용 PlatformIO + Arduino 프로젝트. 학습용 프로젝트이며, `docs/01-learning-roadmap.md` 에 단계별 진행 계획이 정리되어 있습니다 (Phase 0 환경 구축 완료, Phase 1 GPIO 기초 진행 예정).

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

DevKitC-1 에는 **USB-C 포트가 두 개** 있습니다: `UART` (온보드 USB-UART 칩 → UART0) 와 `USB` (네이티브 USB → ESP32-S3 D+/D-). macOS 에서는 각각 다른 `/dev/cu.usbmodem*` 으로 잡힙니다.

현재 `ARDUINO_USB_CDC_ON_BOOT=1` 설정 때문에, `Serial` 은 **네이티브 USB 포트로만 출력**합니다. UART 포트에 케이블을 꽂으면 ROM 부트 메시지는 보이지만 스케치의 `Serial.print(...)` 는 **하나도 보이지 않습니다** — 이게 보드가 멈춘 것처럼 보여서 헷갈리기 쉽습니다. "출력이 안 나옴" 이슈를 디버깅할 때 가장 먼저 확인할 것은 **케이블이 어느 포트에 꽂혔는가** 입니다.

양쪽 모두로 출력하고 싶으면, `Serial` 과 함께 `Serial0` (항상 UART0) 도 같이 사용하면 됩니다.

## 자주 쓰는 명령어

사용자의 `~/.zshrc` 에 `alias pio="$HOME/.platformio/penv/bin/pio"` 가 등록되어 있습니다. 사용자에게 명령어를 안내할 때는 전체 경로 대신 `pio` 를 사용하세요.

| 명령어                         | 용도                                                                |
| ------------------------------ | ------------------------------------------------------------------- |
| `pio run -t upload -t monitor` | **메인 개발 루프** — 빌드, 업로드, 시리얼 모니터까지 한 번에 (포트 잠금 자동 처리) |
| `pio run`                      | 빌드만                                                              |
| `pio device monitor`           | 시리얼 모니터 열기 (115200 baud, `platformio.ini` 에 설정)        |
| `pio device list`              | 사용 가능한 시리얼 포트 목록                                        |
| `pio run -t clean`             | 빌드 결과물 청소 (Flash/PSRAM 설정 변경 후 권장)                  |

CI 나 스크립트에서 포트가 다를 때는 `--upload-port /dev/cu.usbmodemXXXXX` 를 명시적으로 전달하세요. 포트 ID 는 USB 포트마다 다릅니다.

## 시리얼 출력 타이밍

펌웨어 부팅 후 USB-CDC 가 재열거되기 때문에, `setup()` 첫 1초 정도의 `Serial.println()` 출력은 보통 유실됩니다. 모니터는 중간에 다시 붙습니다 (`Disconnected ... Reconnecting`). 다음 두 가지 방법이 효과적입니다:

1. `setup()` 맨 위에 `delay(3000)` — 단순한 블라인드 대기
2. `loop()` 에서 주기적으로 다시 출력 — 모니터를 언제 열든 상태를 다시 확인 가능

"부팅 출력이 안 보임" 문제를 디버깅할 때, 모니터를 열어둔 상태에서 보드의 RST 버튼을 누르라고 안내하면 가장 빠르게 `setup()` 출력을 확인할 수 있습니다.

## 디렉토리 구조

- `src/main.cpp` — 단일 파일 Arduino 스케치 (현재는 깨끗한 골격만)
- `platformio.ini` — 보드 설정 (기본값으로 무작정 재생성하지 말 것)
- `docs/01-learning-roadmap.md` — 단계별 학습 계획. Phase 가 끝나면 체크박스 업데이트
- `include/`, `lib/`, `test/` — 아직 사용 안 함 (PlatformIO 스캐폴딩, 각 폴더의 `README` 참고)
