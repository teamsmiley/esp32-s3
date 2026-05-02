# ESP32-S3 N16R8 학습 로드맵

## 보드 정보

- **모델**: ESP32-S3 DevKitC-1 N16R8
- **N16**: 16 MB Flash
- **R8**: 8 MB Octal SPI PSRAM
- **CPU**: Xtensa LX7 듀얼코어, 240 MHz
- **무선**: Wi-Fi 2.4GHz + Bluetooth 5.0 (LE)
- **USB-C 포트 2개**: UART (USB-UART 칩 경유) / USB (네이티브)

## 핵심 명령어

### 자주 쓰는 명령

| 명령                           | 용도                                 |
| ------------------------------ | ------------------------------------ |
| `pio run`                      | 빌드만                               |
| `pio run -t upload`            | 빌드 + 업로드                        |
| `pio run -t upload -t monitor` | 빌드 + 업로드 + 시리얼 모니터 (자동) |
| `pio device monitor`           | 시리얼 모니터만                      |
| `pio run -t clean`             | 빌드 결과물 청소                     |
| `pio device list`              | 연결된 시리얼 포트 목록              |

### 시리얼 모니터 단축키

- `Ctrl+C`: 모니터 종료
- `Ctrl+T` 다음 `Ctrl+H`: 도움말

---

## 완료한 작업 (Phase 0: 환경 구축)

- [x] PlatformIO 설치 확인 및 alias 등록 (`pio`)
- [x] N16R8 보드 설정 (`platformio.ini` 의 Flash/PSRAM/USB CDC 옵션)
- [x] 첫 펌웨어 빌드 + 업로드 성공
- [x] PSRAM 8 MB / Flash 16 MB 인식 확인
- [x] 시리얼 모니터로 디버그 출력 확인
- [x] USB-CDC vs UART 포트 차이 학습
- [x] 온보드 RGB LED (GPIO48) 제어 (`neopixelWrite`)

---

## 학습 단계 (Phase 1 ~ 5)

### Phase 1: GPIO 기초 (난이도: 쉬움)

**목표**: 입출력 핀을 다루는 감각 익히기

- [x] **1-1. RGB LED 색상 제어**
  - `neopixelWrite(pin, r, g, b)` 로 다양한 색상 만들기
  - HSV → RGB 변환으로 무지개 효과
- [x] **1-2. BOOT 버튼 입력 받기**
  - GPIO0 = BOOT 버튼 (보드 내장)
  - `pinMode(0, INPUT_PULLUP)`, `digitalRead(0)` 사용
  - 버튼 눌림 검출 (debounce 포함)
- [x] **1-3. 버튼으로 LED 색상 토글**
  - 1-1 + 1-2 결합
  - 누를 때마다 색상 순환

**배우는 개념**: GPIO, pull-up resistor, digital I/O, debouncing

---

### Phase 2: 시간과 멀티태스킹 (난이도: 쉬움-중간)

- [ ] **2-1. millis() 기반 비동기 깜빡임**
  - `delay()` 의 한계 이해
  - 여러 작업 동시 처리 (LED 깜빡 + 버튼 감시)
- [ ] **2-2. FreeRTOS 태스크 분리**
  - ESP32는 진짜 RTOS — `xTaskCreate()` 로 코어별 태스크 분배
  - LED 태스크는 코어 0, 시리얼 태스크는 코어 1

**배우는 개념**: non-blocking code, task scheduling, dual-core

---

### Phase 3: 무선 통신 (난이도: 중간)

- [ ] **3-1. WiFi 연결**
  - `WiFi.begin(ssid, password)`
  - IP 주소 출력
  - 연결 끊김 자동 재접속
- [ ] **3-2. HTTP 클라이언트**
  - 외부 API 호출 (예: 날씨, 시간)
  - JSON 파싱 (`ArduinoJson` 라이브러리)
- [ ] **3-3. mDNS / NTP 시간 동기**
  - `esp32-s3.local` 같은 이름 부여
  - 인터넷 시간으로 RTC 맞추기

**배우는 개념**: TCP/IP stack, JSON, async networking

---

### Phase 4: 웹서버 (난이도: 중간)

- [ ] **4-1. 기본 웹서버**
  - 브라우저로 `http://<esp_ip>/` 접속하면 페이지 응답
  - `WebServer` 라이브러리 사용
- [ ] **4-2. LED 웹 컨트롤러**
  - 웹페이지에서 색상 선택 → ESP32가 받아서 LED 변경
  - REST 엔드포인트 설계 (`/led?r=255&g=0&b=0`)
- [ ] **4-3. WebSocket 실시간 통신**
  - 폴링 없이 양방향 데이터 교환
  - 센서 데이터 실시간 그래프

**배우는 개념**: HTTP, REST, WebSocket, HTML/CSS/JS embedded

---

### Phase 5: PSRAM / 파일시스템 (난이도: 중간)

- [ ] **5-1. PSRAM에 큰 배열 할당**
  - 일반 heap 360KB vs PSRAM 8MB 차이 체감
  - `ps_malloc()`, `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
- [ ] **5-2. LittleFS 파일시스템**
  - Flash에 파일 저장/읽기
  - 설정 파일, 로그 파일
- [ ] **5-3. 카메라 / 디스플레이 드라이버**
  - PSRAM이 진짜 빛을 발하는 영역 (프레임 버퍼)
  - 추후 OV2640 카메라 또는 ST7789 LCD 추가 시

**배우는 개념**: heap caps, filesystems, frame buffers

---

## 다음 추천 순서

`Phase 1-2` (BOOT 버튼으로 LED 색상 바꾸기) → 가장 즉각적인 성취감을 주는 첫 인터랙션입니다.

---

## 참고 링크

- [ESP32-S3 DevKitC-1 공식 문서](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [Arduino-ESP32 API 레퍼런스](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [PlatformIO ESP32 플랫폼](https://docs.platformio.org/en/latest/platforms/espressif32.html)
