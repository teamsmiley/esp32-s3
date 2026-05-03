# TODO

MATE Floats 2026 미션 진행 항목. 완료된 것은 아래 `## 완료` 섹션 참고. 미션 규정 / 점수 상세는 `docs/2026_MATE_Floats_분석.md`.

## 미션 — 소프트웨어

- [ ] **자율 프로파일 시퀀스** — 미션 점수 ③④ (각 25점, 합 50점)
  - 수면 → 수심 2.5m 하강 → 30초 유지 → 수심 40cm 상승 → 30초 유지 × **2회**
  - 시퀀스 상태 머신 (descend / hold-deep / ascend / hold-shallow / 다음 사이클)
  - 사이클 사이 전환 조건 명시
  - 트리거: 무선 명령 `START` 가 이미 stub 으로 들어가 있음 — 시퀀스 본체 구현 시 stub 제거하고 연결

- [ ] **30초 유지 판정 로직**
  - 2.5m 유지: 2.27~2.83m 범위 (플로트 하단 기준, ±33cm)
  - 40cm 유지: 7~73cm 범위 (플로트 상단 기준, ±33cm)
  - 5초마다 패킷 7개 연속 (0/5/10/15/20/25/30초) 모두 범위 안이어야 인정. 한 번이라도 이탈 시 처음부터 재측정

- [ ] **깊이 제어 PID** — 부력 엔진 PWM 출력
  - 부력 엔진 하드웨어 도착 후 튜닝
  - 노이즈 ±4mm 대비 충분한 deadband

- [ ] **수면 돌파 / 얼음 접촉 회피** — 패널티 -5점
  - 상단 안전 임계값 이하로 올라가면 즉시 약하게 하강
  - 하강 시 가속도 / 깊이 변화율 확인 (얼음 충돌 감지)

## 미션 — 하드웨어

- [ ] **부력 엔진 결정 + 발주** — 미션의 핵심 (프로파일 50점)
  - 후보: 시린지 펌프 / 피스톤 / 공기 블래더
  - 모터로 공기 또는 액체 이동만 인정. 프로펠러·워터젯 등 직접 추력 **불가**
- [ ] **모터 드라이버** — 부력 엔진 PWM 제어용 (선정 후 발주)
- [ ] **배터리 선정** — NiMH 또는 AGM, 최대 12VDC / 5A
  - LiPo, 알카라인, 12V 야외용 충전 배터리 **불허**
  - 부력 변환 모드의 Full Load Amps 측정 → DOC-004 에 기재
- [ ] **단일 퓨즈** — ATO 블레이드 / MINI 블레이드 / 카트리지, 32VDC 정격 이상
  - 배터리 양극 단자에서 5cm 이내 설치
  - 투명 하우징 또는 외함 제거 시 육안 확인 가능해야 함
- [ ] **압력 해방 장치** — 2.5cm 이상 직경 구멍 또는 팝오프 엔드캡 (O링 밀봉)
  - 압력 해방 밸브, 호스 클램프, Twist-Tite 방식 **불가**
- [ ] **케이스 설계** — 높이 1m 미만 (안테나 포함), 지름 18cm 미만
- [ ] **회수 장치 (2026 신규 요건)** — 5cm 이상 폭 / 5cm 이상 돌출
  - U-볼트 (#310 이상) 또는 로프·와이어 루프
  - 18cm 지름 한계 예외 적용 가능 (회수 장치에 한해)

## 미션 — 문서

- [ ] **DOC-004 제출** (대회 전 필수, 총 3페이지)
  - 본문 2페이지: 장치 사진/다이어그램, 배터리 종류·사진, 퓨즈 사진, Full Load Amps (대기 모드 + 부력 변환 모드), 부력 엔진 설명, 통신 방식 설명, 배터리 팩 설계 근거
  - SID 1페이지: 회로 식별 다이어그램, 표준 퓨즈 기호, Full Load Amps 측정값
  - 지역 대회는 주최 측에 제출 여부 확인 (미제출 시 0점)
- [ ] **수심-시간 그래프 도구** — 미션 점수 ⑥ (10점)
  - 도구 자체는 완성 (`tools/read_and_graph.py`, 완료 섹션 참고)
  - 남은 일: **실물 미션 데이터로 한 번 end-to-end 리허설** (station USB → 캡처 → CSV → PNG)
  - 시연 15분 안에 그래프까지 나오는지 시간 측정

## 완료

- [x] **MS5837-30BA 깊이 센서 동작 확인**
  - I2C addr `0x76`, SDA=GPIO8, SCL=GPIO9
  - 노이즈 ±4mm 수준. 16회 평균 부팅 시 자동 0점 보정 (`calibrateZero()`)
  - BOOT 버튼으로 재보정 (수면에서 마지막 보정용 — 단, 물속에선 무선 명령 필요. TODO 위쪽 `CMD_ZERO` 참고)

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
  - 빌드 명령은 각 디렉토리 안에서 (`cd float && pio run -t upload -t monitor`)
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
