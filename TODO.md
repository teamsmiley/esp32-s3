# TODO

MATE Floats 2026 미션 진행 항목. 완료된 것은 아래 `## 완료` 섹션 참고. 미션 규정 / 점수 상세는 `docs/2026_MATE_Floats_분석.md`.

## 미션 — 소프트웨어

(소프트웨어 측 신규 항목 없음.)

**ESP32 의 역할**: 깊이 측정 + 5초 패킷 + LittleFS 로깅 + 무선 명령 응답 (Z/D/P/R) — 모두 구현 완료
**ESP32 가 안 하는 것**: 모터 제어 / 잠수 시퀀스 / PID — 별도 **하드웨어 컨트롤러** 가 모터 관리

미션 점수 ②⑤⑥ (합 25점) 은 ESP32 만으로 커버. ③④ (합 50점) 의 자율 프로파일은 하드웨어 컨트롤러가 모터를 어떻게 운용하는가에 달림 — ESP32 는 그 동안 깊이 데이터만 기록하면 됨.

## 미션 — 하드웨어

- [ ] **단일 퓨즈** — ATO 블레이드 / MINI 블레이드 / 카트리지, 32VDC 정격 이상
  - 배터리 양극 단자에서 5cm 이내 설치
  - 투명 하우징 또는 외함 제거 시 육안 확인 가능해야 함
- [ ] **압력 해방 장치** — 2.5cm 이상 직경 구멍 또는 팝오프 엔드캡 (O링 밀봉)
  - 압력 해방 밸브, 호스 클램프, Twist-Tite 방식 **불가**
- [ ] **회수 장치 (2026 신규 요건)** — 5cm 이상 폭 / 5cm 이상 돌출
  - U-볼트 (#310 이상) 또는 로프·와이어 루프
  - 18cm 지름 한계 예외 적용 가능 (회수 장치에 한해)

## 미션 — 문서

- [ ] **DOC-004 제출** (대회 전 필수, 총 3페이지)
  - 본문 2페이지: 장치 사진/다이어그램, 배터리 종류·사진, 퓨즈 사진, Full Load Amps (대기 모드 + 부력 변환 모드), 부력 엔진 설명, 통신 방식 설명, 배터리 팩 설계 근거
  - SID 1페이지: 회로 식별 다이어그램, 표준 퓨즈 기호, Full Load Amps 측정값
  - 지역 대회는 주최 측에 제출 여부 확인 (미제출 시 0점)
- [ ] **end-to-end 리허설** — 미션 점수 ⑥ (10점)
  - 도구는 모두 완성 (`tools/read_and_graph.py`, station LittleFS, 두 터미널 흐름)
  - 남은 일: **실물 미션 데이터로 한 번 전체 흐름 검증** (수면 부유 → Z → 잠수 → 회수 → D → 그래프 생성)
  - 시연 15분 안에 PNG 까지 나오는지 시간 측정 + 트러블슈팅 항목 정리

## 완료

- [x] **배터리 준비 완료** (팀에서 처리 — 내가 신경쓸 필요 없음)
  - 규정 준수 항목 (NiMH/AGM, 12VDC/5A 한계) 은 팀에서 결정·발주
  - Full Load Amps 측정값은 DOC-004 작성 시 팀에서 받아오기

- [x] **케이스 제작 완료**
  - MATE 규정 준수: 높이 1m 미만 (안테나 포함), 지름 18cm 미만
  - 회수 장치는 별도 항목 (예외 허용 부분) — 위 미완 섹션 참고

- [x] **부력 메커니즘 — 모터 + 별도 컨트롤러** (미션 점수 ③④ 관련 하드웨어 분담)
  - 모터 구동 방식으로 확정 (시린지 펌프 / 피스톤 / 공기 블래더 후보 중)
  - **별도 하드웨어 컨트롤러** 가 모터 관리 — ESP32 는 모터 제어 미참여 (PID/PWM/시퀀스 코드 불필요)
  - MATE 규정 준수: 모터로 공기/액체 이동만 인정 (프로펠러·워터젯 등 직접 추력 불가)
  - 후속 (팀 영역, ESP32 외): 모터·드라이버 모델 확정 + 발주 + Full Load Amps 측정 (DOC-004 용)

- [x] **셋업 + 시연자 가이드 문서** (`docs/prerequisites.md` + `prerequisites_ko.md`)
  - macOS / Windows 양쪽 명령 (uv 설치 + git clone + `cd tools && uv sync && source .venv/bin/activate`)
  - 두 터미널 흐름 (모니터 + 그래프) — 시리얼 포트 충돌 회피
  - 대회 전날 점검 체크리스트 + OS 별 함정 (USB 라벨, CDC 드라이버, 데이터 케이블)
  - 영어/한국어 두 버전 동기화

- [x] **미션 흐름 다이어그램** (`docs/mission_flow_diagram.excalidraw` + `Floats.png`)
  - 3 swimlane (Laptop / Station / Float) × 7 phase, Excalidraw 로 작성
  - 잠수 단계는 큰 물색 배경으로 무선 침묵 시각화
  - 점수 단계 (②⑤⑥) 주황 뱃지로 강조
  - 하단에 데이터 흐름 요약 (sensor → LittleFS → ESP-NOW → received.log → PNG)
  - PNG 로 export 해서 README 의 hero image 로 사용

- [x] **README 시연자 관점 재구성** (`README.md` + `README_ko.md`)
  - 빌드/업로드 명령 모두 제거 — 사용자(시연자)에게 불필요
  - Mermaid 다이어그램 → `Floats.png` 로 교체
  - 셋업/명령 디테일은 prerequisites 로 분리, README 는 흐름·역할 설명만
  - 영어/한국어 두 버전 모두 동일 구조

- [x] **PlatformIO 를 tools/ uv 가상환경으로 통합** (`tools/pyproject.toml`)
  - 시스템 전역 `pio` 설치 불필요. `uv sync` 한 번이면 platformio + pyserial + matplotlib 모두 설치
  - 활성화 후 `pio` 그대로 사용. CI/시연자 환경 모두 단일 셋업
  - 보드 디렉토리는 `-d ../<board>` 옵션으로 지정 (개발자용. CLAUDE.md 참고)

- [x] **station 측 LittleFS 수신 저장** (`station/src/main.cpp`)
  - 모든 RX 패킷을 `received.log` 에 자동 append (라이브 미션 + DUMP 응답 둘 다)
  - 부팅 시 자동 백업 안 함 (재부팅으로 데이터 손실 방지) — 명시적 `E` 키로만 삭제
  - 새 키 매핑:
    - `R` = `received.log` 시리얼 dump (`---- BEGIN/END received.log ----` 마커 포함, Python 도구가 자동 탐지)
    - `E` = `received.log` 삭제 (새 미션 전 정리)
    - `I` = 파일 크기 + FS 사용량 출력
  - 회수 후 호스트 PC 에서 `python read_and_graph.py` 한 번이면 R 자동 송신 + 그래프 생성

- [x] **MS5837-30BA 깊이 센서 동작 확인**
  - I2C addr `0x76`, SDA=GPIO8, SCL=GPIO9
  - 노이즈 ±4mm 수준. 16회 평균 부팅 시 자동 0점 보정 (`calibrateZero()`)
  - BOOT 버튼으로 재보정 가능. 물속에서는 station 의 `Z` 키 → `ZERO` 무선 명령 사용 (양쪽 다 구현됨)

- [x] **미션 데이터 패킷 포맷터** (`formatPacket()`)
  - 형식: `PVPHSROV HH:MM:SS XX.X kPa X.XX meters`
  - 시간: 미션 시작 (부팅 또는 재보정) 이후 경과 시간 (`millis() - missionStartMs`)
  - 압력: MS5837 의 mbar → kPa 변환 (`× 0.1`)
  - 깊이: 0점 보정 적용된 `calibratedDepth()` 사용

- [x] **5초 정확 송출 스케줄러** (millis 기반, drift 없음)
  - 누적 방식: `nextPacketMs += PACKET_INTERVAL_MS` (한 박자 늦어도 다음 박자에서 따라잡음)
  - 첫 패킷은 보정 직후 즉시, 이후 정확히 5초 간격
  - 미션 점수 ③④ "30초 유지 = 패킷 7개 연속" 판정의 시간 기준이 됨

- [x] **BOOT 버튼 디바운스 + 재보정 트리거**
  - 50ms 디바운스, 누른 순간 1회 트리거 (release 까지 폴링 대기)
  - 재보정 시 `missionStartMs` 도 리셋해 시간이 `00:00:00` 부터 재시작

- [x] **ESP-NOW 송수신 통신 검증** (float → station)
  - WiFi STA 모드, AP 미접속, 페어링 절차 없음
  - Arduino-ESP32 v2.x 콜백 시그니처 사용 (`const uint8_t *mac, const uint8_t *data, int len`)
  - 5초마다 패킷이 시리얼 + 무선 양쪽으로 송출, 짝 station 시리얼에 동일 내용 수신

- [x] **ESP-NOW unicast + MAC 화이트리스트** (대회 환경 충돌 방지)
  - 배경: 대회장에서 여러 팀이 동시에 ESP-NOW 사용. broadcast 면 우리 station 이 다른 팀 패킷도 받고, 우리 패킷이 다른 팀 station 에 도달
  - 적용:
    - `float/src/main.cpp` — `stationMac` 으로 unicast 송신 (broadcast 주소 폐기)
    - `station/src/main.cpp` — `FLOAT_MAC` 화이트리스트 검사. 다른 팀 패킷은 `[ignored ...]` 로 흘려보냄
  - 현재 박혀있는 MAC: float `AC:A7:04:EE:43:B0`, station `AC:A7:04:13:3A:E8`
  - 만약 보드 교체 시 두 파일의 MAC 상수를 새 보드 MAC 으로 갱신해야 함

- [x] **PlatformIO 프로젝트 분리** (`float/`, `station/`)
  - 두 보드의 펌웨어가 다르므로 디렉토리 단위로 분리 (단일 env 다중 source filter 보다 직관적)
  - 빌드 명령은 `tools/` venv 활성화 후 `pio run -d ../<board> -t upload -t monitor` (활성화된 PlatformIO 사용)
  - station 측은 MS5837 라이브러리 의존성 없음 (`station/platformio.ini` 에 `lib_deps` 없음) — 빌드 사이즈 작음

- [x] **로컬 데이터 로깅 (LittleFS)** — 미션 점수 ⑤⑥ 의 토대
  - 16MB Flash 위 LittleFS, 부팅 시 자동 마운트 + 직전 미션 로그를 `mission.log.bak` 으로 백업
  - 5초 패킷 송출 시 시리얼 + 무선 + 디스크 (`/mission.log`) 세 갈래 동시 기록
  - 무선이 일시적으로 도달 안 해도 디스크엔 항상 남음 → 회수 후 일괄 dump 패턴
  - dump 시 패킷 간 50ms delay (ESP-NOW 큐 보호)

- [x] **데이터 읽기 + 그래프 도구** (`tools/read_and_graph.py`) — 미션 점수 ⑥ 의 토대
  - Python + pyserial + matplotlib (`tools/pyproject.toml` + `uv.lock`, uv 로 격리)
  - 옵션 없는 단일 흐름: 자동 탐지 → `R` 송신 → station LittleFS dump 수신 → `received.log` + `received.png`
  - END 마커 자동 감지로 종료 (Ctrl+C 불필요)
  - 포트 충돌 회피: 도구가 station 시리얼을 단독 점유 → `pio device monitor` 와 동시 실행 안 함

- [x] **무선 양방향 명령 인프라** (station → float ESP-NOW)
  - 4글자 텍스트 명령 + station 측 키 매핑 (D/Z/P/S/H)
  - float 콜백 → 플래그 set → `loop()` 에서 처리 (ISR-like 안전성)
  - station MAC 화이트리스트 검사로 다른 팀 명령 무시
  - 구현 상태:
    - [x] `DUMP` — LittleFS 로그 일괄 송신
    - [x] `ZERO` — 깊이 0점 재보정 + `ZERO_OK offset=X.XXXX m` 응답
    - [x] `PING` — `PONG` 응답 (연결 확인)
    - [ ] `STAR` (START) — stub. 자율 프로파일 시퀀스 구현 시 본체 연결
