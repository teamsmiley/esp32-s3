# 동작 원리 — 플로트 미션 내부 구조

이 문서는 플로트 펌웨어가 MATE Floats 2026 미션을 자율적으로 어떻게 수행하는지 설명합니다. 상태 기계, 부력 엔진, 깊이 좌표계, 페일세이프 계층을 다룹니다.

미션 명세 자체(점수, 패킷 형식, 규정)는 [`2026_MATE_Floats_분석.md`](2026_MATE_Floats_분석.md)를 보세요.
두 보드 운용 절차는 [`prerequisites_ko.md`](prerequisites_ko.md)를 보세요.

---

## 1. 미션 한 문단 요약

운영자가 station에서 `S`를 누르면, 플로트가 추가 개입 없이 **3회 수직 프로파일**을 실행합니다. 각 프로파일은: **2.5m**(플로트 하단부 기준)까지 하강해 **30초** 유지(±33cm 허용 밴드), 다음으로 **40cm**(플로트 상단부 기준)까지 상승해 **30초** 유지, 마지막으로 수면 복귀(다음 프로파일 시작 가능 상태). 전체 시퀀스는 fake-depth 책상 테스트에서 약 3분; 실제 물에서는 하강/상승 이동 시간이 추가됩니다.

---

## 2. 상태 기계

미션은 `loop()` 안에서 **non-blocking 상태 기계**로 동작합니다 — depth 센서, 패킷 송신, BOOT 버튼, ESP-NOW 수신이 모두 병렬로 계속 동작합니다.

```text
              ┌──────────┐
              │  IDLE    │ ◄──── 'X' (abort) 어느 상태에서도
              └────┬─────┘
                   │ 'S' (start)
                   ▼
           ┌───────────────┐
           │   DESCEND     │  모터 풀스피드 흡입 — 빠른 하강
           └───────┬───────┘
                   │ depth ≥ 2.27 m  (DEEP_MIN_M)
                   ▼
           ┌───────────────┐
           │  HOLD_DEEP    │  2.55m 미드포인트 bang-bang, 30초 타이머
           └───────┬───────┘
                   │ [2.27, 2.83] 밴드 안에서 30초 경과
                   ▼
           ┌───────────────┐
           │ ASCEND_SHALLOW│  모터 OFF — 자연 부력으로 상승
           └───────┬───────┘
                   │ depth ≤ 1.0348 m  (SHALLOW_MAX_M, bottom 기준)
                   ▼
           ┌───────────────┐
           │ HOLD_SHALLOW  │  0.70m 미드포인트 bang-bang, 30초 타이머
           └───────┬───────┘
                   │ [0.3748, 1.0348] 밴드 안에서 30초 경과
                   ▼
           ┌───────────────┐
           │   SURFACE     │  모터 OFF — 자연 부력으로 상승
           └───────┬───────┘
                   │ depth ≤ 0.20 m  (SURFACE_M, bottom 기준)
                   ▼
            profileIndex++
                   │
                   ├─ 프로파일 더 있으면 → DESCEND
                   └─ 없으면              → DONE
```

### SURFACE가 왜 중요한가

SURFACE는 **다음 프로파일로 넘어가는 유일한 경로**입니다. SURFACE 검사가 발동되지 않으면 플로트가 영원히 `MS_SURFACE`에 머무르고, `[STATE] -> DESCEND (profile N/3)` 전환이 한 번도 로그되지 않으며, 미션이 완료되지 않습니다. 검사는 bottom 기준(`reportedDepth() ≤ SURFACE_M`)으로 수행 — 센서가 미들에 있으면 플로트가 자연 부유 상태일 때 bottom이 약 15cm 정도(미들 + 6 in)로 떨어져서 20cm 임계값 안에 들어옵니다.

---

## 3. 깊이 좌표계

플로트는 **12인치(30.48cm) 높이**이고, MS5837 depth 센서는 **바닥에서 약 1인치(2.54cm) 위**에 마운트되어 있습니다 (하우징을 위한 작은 standoff).

```text
   ━━━━━ ← top
  ┃     ┃
  ┃     ┃
  ┃ 12  ┃
  ┃ in  ┃
  ┃     ┃
  ┃  ●  ┃ ← 센서 (바닥에서 1in 위, bottom - 0.0254m을 읽음)
   ━━━━━ ← bottom (= 센서값 + 0.0254 m)
```

단일 헬퍼 함수가 raw 센서값을 변환합니다:

| 함수              | 반환값                         | 용도                                            |
| ----------------- | ------------------------------ | ----------------------------------------------- |
| `reportedDepth()` | 센서값 + 0.0254m (플로트 하단) | 모든 HOLD 밴드, SURFACE 검사, 패킷 출력, 그래프 |

미션이 **혼합 기준**으로 hold 목표를 명시(2.5m는 bottom, 40cm는 top)하지만, 내부적으로는 모든 것을 bottom 깊이(`reportedDepth()`)로 정규화해 일관된 밴드 비교를 합니다. SURFACE 검사도 bottom 기준(`reportedDepth() ≤ 0.20 m`) — 바닥 근처 마운트 덕분에 플로트가 떠오를 때 센서가 가장 오래 물에 잠겨 있고, 탑 마운트라면 0m에서 saturation되어 잃어버릴 깊이 신호가 연속적으로 유지됩니다.

### 미션 밴드 (bottom 기준, 코드상)

```c
DEEP_MIN_M    = 2.27 m   // bottom 2.5m, -23cm
DEEP_MAX_M    = 2.83 m   // bottom 2.5m, +33cm
SHALLOW_MIN_M = 0.07 m + FLOAT_HEIGHT = 0.3748 m   // top 7cm → bottom 변환
SHALLOW_MAX_M = 0.73 m + FLOAT_HEIGHT = 1.0348 m   // top 73cm → bottom 변환
```

패킷 출력(`PVPHSROV HH:MM:SS XX.X kPa Y.YY meters`)은 `reportedDepth()`를 보고하므로, 채점 팀은 `2.5m`, `0.4m + 12in` 미션 목표와 직접 비교 가능한 값을 봅니다.

---

## 4. 부력 엔진 (단방향 펌프)

플로트는 L298N H-bridge로 구동되는 단일 DC 펌프를 사용하며, `ENB`에 PWM 속도 제어가 적용됩니다.

| 핀    | GPIO | 역할                             |
| ----- | ---- | -------------------------------- |
| `IN3` | 17   | 방향 A (HIGH = 흡입)             |
| `IN4` | 18   | 방향 B (항상 LOW — 역회전 안 함) |
| `ENB` | 4    | PWM 속도 (0 = 정지, 255 = 풀)    |

**작동 원리:**

- **펌프 ON** (흡입 방향): 물이 플로트 안으로 유입 → 무거워짐 → 가라앉음
- **펌프 OFF**: 잔여 양의 부력으로 자연스럽게 떠오름 → 역방향 펌프 불필요

속도 프리셋:

| 상수               | 값         | 사용 시점                                      |
| ------------------ | ---------- | ---------------------------------------------- |
| `MOTOR_SPEED_FULL` | 255 (100%) | DESCEND 상태 — 빠른 하강으로 미션 시간 절약    |
| `MOTOR_SPEED_HOLD` | 90 (~35%)  | HOLD 밴드 — 오버슈트 방지를 위한 부드러운 보정 |

### HOLD 밴드 컨트롤러 — 미드포인트 bang-bang

HOLD 상태에서는 플로트가 대부분 위로 표류함(자연 부력). ±33cm 밴드 안에 유지하려면:

```c
midpoint = (lo + hi) / 2;
if (depth < midpoint) motor on (HOLD speed);   // 목표 쪽으로 살짝 가라앉히기
else                  motor off;               // 자연스럽게 떠오르도록
```

미드포인트 주변에서 작은 진동을 만듭니다 — 자연 상승 속도가 너무 빠르지 않으면 ±33cm 밴드 안에 머무릅니다.

### 30초 hold 타이머

각 HOLD 상태는 non-blocking 타이머를 실행합니다:

- 밴드 안 → hold 시간 누적
- 밴드 이탈 → **타이머 0으로 리셋** (미션 규칙: "범위 이탈 = 처음부터 재시작")
- 30초 도달 → 다음 상태로 전환

---

## 5. 명령어

단일 문자 키. float 시리얼 모니터에서 직접 입력하든, station에서 무선으로 보내든 동일하게 동작.

### 미션 제어

| 키  | 무선   | 동작                              |
| --- | ------ | --------------------------------- |
| `S` | `STAR` | 3회 프로파일 미션 시작            |
| `X` | `ABRT` | abort — 모터 정지, IDLE로 복귀    |
| `T` | `TEST` | 10초 모터 속도 램프 self-test     |
| `C` | `CALI` | 수중 HOLD PWM 자동 캘리브레이션 (`/cali.txt`에 저장, 부팅 시 자동 로드) |
| `Z` | `ZERO` | depth 영점 재교정 (16샘플 평균)   |
| `P` | `PING` | 연결 확인 (`PONG` 응답)           |
| `D` | `DUMP` | LittleFS 미션 로그 전체 무선 전송 |

### 책상 테스트 명령어 (fake depth)

물 없이 상태 기계를 돌릴 수 있게 센서 reading을 오버라이드합니다.

| 키  | 무선   | fake bottom-depth | 용도                      |
| --- | ------ | ----------------- | ------------------------- |
| `F` | `FAKE` | toggle on/off     | fake 모드 활성/비활성     |
| `0` | `FK0M` | 0.00 m            | 수면 프리셋 (top이 물 위) |
| `2` | `FK2M` | 2.50 m            | deep 목표                 |
| `4` | `FK4M` | 0.70 m            | shallow 목표              |
| `+` | `FKUP` | +0.10 m           | 더 깊게 nudge             |
| `-` | `FKDN` | -0.10 m           | 더 얕게 nudge             |

전체 dry-run 시퀀스:

```text
F  ← fake 모드 ON
S  ← 미션 시작
2  ← fake depth = 2.50m → HOLD_DEEP
   (30초 대기, 패킷 6개)
4  ← fake depth = 0.70m → HOLD_SHALLOW
   (30초 대기)
0  ← fake depth = 0.00m → SURFACE → 다음 프로파일
2 → 30초 대기 → 4 → 30초 대기 → 0   (프로파일 2)
2 → 30초 대기 → 4 → 30초 대기 → 0   (프로파일 3)
   → MISSION_DONE → STATE = DONE
```

총 책상 테스트 시간: 약 3분.

---

## 6. 페일세이프 계층

### 비상 깊이 (`MAX_DEPTH_M = 3.20 m`)

reportedDepth가 3.20m을 초과하면(정상 프로파일 중에는 불가능) 펌웨어는:

1. 펌프 정지
2. `MS_SURFACE`로 강제 전환
3. 자연 부력으로 떠오르게 둠

### DESCEND 워치독 (`STATE_TIMEOUT_MS = 90 s`)

플로트가 90초 안에 deep 밴드에 도달하지 못하면(펌프 막힘, 센서 멈춤 등):

1. 펌프 정지
2. `MS_SURFACE`로 강제 전환

상승 단계(`ASCEND_SHALLOW`, `SURFACE`)에는 **타임아웃이 없음** — 자연 부력은 몇 분이 걸릴 수 있고, 특히 펌프에 잔여 물이 있을 때 그렇습니다. 운영자의 `X` 키가 수동 오버라이드.

### 부팅 시 모터 안전

`setup()`에서 `pinMode(MOTOR_ENB, OUTPUT)` 직후 `motorStop()` 호출. 글리치로 GPIO가 잠시 HIGH였더라도 펌프가 리셋 후 확실히 꺼져 있도록 보장.

### LittleFS 로그 백업

매 부팅마다 이전 `/mission.log`가 `/mission.log.bak`으로 이름 변경 → 예상치 못한 리셋이 직전 미션 데이터를 지울 수 없음.

### HOLD 속도 캘리브레이션

`MOTOR_SPEED_HOLD`는 부팅 시 `/cali.txt`에서 로드. 파일이 없거나, 형식이 잘못됐거나, 1–255 범위를 벗어나면 하드코딩된 기본값(90/255)으로 fallback하고 `caliPending` 플래그를 켭니다.

`/cali.txt` 채우는 두 가지 경로:

- **수동 `C` / `CALI`** — 미션 전 명시적 캘리브. 플로트가 수면에서 하강 → 7개 PWM(60-180) 4초씩 sweep → 가장 드리프트 작은 값 저장 → 수면 복귀.
- **자동 inline** — 시합 미션 중 첫 `MS_HOLD_DEEP` 진입 시점에 `caliPending`이 켜져 있으면, 더 짧은 4-PWM × 3초 sweep(~12초)을 30초 hold 타이머 **시작 전**에 실행. "미션 물에 사전 테스트 못 하는 경우"를 자동 처리.

`X`로 어느 경로든 중간 abort 가능하며 `/cali.txt`는 변경 안 됨. `caliPending`이 한 번 클리어되면(부팅 시 파일 로드 성공 또는 sweep 완료), 이후 미션은 캘리브 단계를 완전히 건너뜀.

---

## 7. 관련 파일

| 파일                      | 역할                                                        |
| ------------------------- | ----------------------------------------------------------- |
| `float/src/main.cpp`      | 펌웨어 전체 로직 (상태 기계, 모터, 센서, ESP-NOW, LittleFS) |
| `station/src/main.cpp`    | 키 입력 → 4바이트 ESP-NOW 명령 forward + RX 로그            |
| `tools/read_and_graph.py` | `received.log` 가져와서 깊이-시간 차트 렌더링 — **2개 라인**(패킷의 BOTTOM + 계산된 TOP) + 미션 목표선(BOTTOM 2.5m, TOP 0.4m) |

해당 상수와 헬퍼 정의를 찾으려면 `float/src/main.cpp`에서 다음 앵커를 검색:

- `#define DEEP_MIN_M` (미션 밴드)
- `enum MissionState` (상태 기계)
- `void missionTick(` (틱마다 상태 평가)
- `bool holdControl(` (미드포인트 bang-bang)
- `float reportedDepth()` (깊이 헬퍼 — bottom 기준, 모든 곳에서 사용)

---

## 8. Calibration ritual

매 배포 전:

1. 플로트를 수면(또는 운영자 손에서 수면 근처)에서 부팅.
2. 부팅 메시지 대기 — `[zero calibration done] offset = X.XXXX m`.
3. 다른 물(짠물, 차가운 물)에 배포 예정이면 실제 그 물에 띄운 상태에서 `Z` 명령(또는 BOOT 버튼)으로 재교정.
4. 첫 패킷이 합리적인 깊이를 표시하는지 확인 (`0.30 meters`가 예상값 — 센서가 top, 공기 중, calibration 직후).

재교정 후 미션 경과 시간 클럭도 리셋됨 — 따라서 `S` 누르기 **전에** calibration 완료.
