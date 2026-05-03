# MATE Floats 2026 — 규정 검증 + 자율 제어 논의

이 문서는 두 가지 질문에 대한 분석을 정리합니다.

1. 한국어 분석 문서 (`2026_MATE_Floats_분석.md`) 가 원본 영어 규정을 정확히 반영하는가?
2. 사람이 잠수 중 플로트를 무선으로 조절해도 되는가?

원본 출처: 2026 MATE ROV Competition Preview Mission — MATE Floats Under the Ice (Updated Oct 01, 2025)

---

## 1. 원본 영어 규정 vs 한국어 분석 문서 — 검증

### 1.1 핵심 결론

| 항목                               | 한국어 문서        | 원본 영어                 | 평가                         |
| ---------------------------------- | ------------------ | ------------------------- | ---------------------------- |
| 프로펠러 추진 가능 여부            | "불인정 방식" 표현 | 명시적 허용 (점수만 절반) | 한국어 표현이 오해 소지 있음 |
| 점수 구조 (총 70점)                | 정확               | 일치                      | OK                           |
| 깊이 범위 (2.5m ±33cm, 40cm ±33cm) | 정확               | 일치                      | OK                           |
| 30초 유지 = 7개 연속 패킷          | 정확               | 일치                      | OK                           |
| 테더/에어라인 불가                 | 정확               | 일치                      | OK                           |
| 배터리 종류 (NiMH/AGM)             | 정확               | 일치                      | OK                           |
| 단일 퓨즈 + 5cm 이내               | 정확               | 일치                      | OK                           |
| 회수 장치 (5cm 폭/돌출)            | 정확               | 일치                      | OK                           |
| 깊이 센서 위치 보정                | **누락**           | 명시됨                    | **추가 필요**                |
| 시각 표시 LED 허용                 | **누락**           | 명시됨                    | **추가 검토 가치**           |
| 추가 프로파일 시도 가능            | **부분적**         | 더 자세함                 | 보완 필요                    |
| 다중 배터리 팩 규칙                | **누락**           | 상세 규정 있음            | HW 팀 확인 필요              |
| 부품 분리/펼침 금지                | "분리형 구조 불가" | 더 명확함                 | OK (해석 동일)               |

### 1.2 프로펠러 / 스러스터 — 핵심 정정

**한국어 문서의 "불인정 방식: 프로펠러 또는 워터젯 추진" 은 금지가 아니라 "부력 엔진 분류 안 됨" 이라는 뜻입니다.**

원본 4페이지:

> "Using motors as thrusters to directly move the float, by turning a propeller or emitting a jet of water, is not using a buoyancy engine. **Companies that do not use a buoyancy engine to complete their vertical profiles will receive fewer points.**"

원본 10페이지, ELEC-NRD-002:

> "The vertical profiling float non-ROV device **may utilize thrusters** but may not include any cameras."

→ 스러스터(=프로펠러) 사용은 규정상 명시적 허용. 카메라만 금지.

**점수 영향**:

| 항목                  | 부력 엔진 | 프로펠러 (다른 방식) | 차이    |
| --------------------- | --------- | -------------------- | ------- |
| ③ 프로파일 1회차 완료 | 10        | 5                    | -5      |
| ③ 2.5m 유지 (1회차)   | 5         | 5                    | 0       |
| ③ 40cm 유지 (1회차)   | 5         | 5                    | 0       |
| ④ 프로파일 2회차 완료 | 10        | 5                    | -5      |
| ④ 2.5m 유지 (2회차)   | 5         | 5                    | 0       |
| ④ 40cm 유지 (2회차)   | 5         | 5                    | 0       |
| **③④ 합계**           | **40**    | **30**               | **-10** |

→ 프로펠러 방식 시 만점이 70 → 60 으로 내려갈 뿐, 실격이나 0점 아님.

### 1.3 깊이 센서 위치 보정 — 누락된 결정적 항목

원본 5페이지:

> "If the float's depth/pressure sensor is not at the bottom of the float, communicate the offset to the station judge. For example, if the float's depth/pressure sensor is 25 cm above the bottom of the float, when the bottom of the float is at 2.5 meters, the pressure sensor would be at 2.25 meters. Thus, the proper range for the depth/pressure sensor would be 2.58 meters to 1.92 meters."

**의미**: MS5837 센서는 플로트 바닥 위 어딘가 회로 기판에 있습니다. 채점 기준은:

- 2.5m 유지 = **플로트 바닥** 기준
- 40cm 유지 = **플로트 상단** 기준

→ 센서 위치를 정확히 측정 후 채점관에게 사전 통지 필수.

**예시 계산**:

플로트 높이 80cm, 센서가 바닥에서 30cm 위, 상단에서 50cm 아래라고 가정.

| 채점 기준                | 플로트 위치  | 센서 측정값             |
| ------------------------ | ------------ | ----------------------- |
| 바닥이 2.5m              | 바닥 = 2.50m | 2.50 - 0.30 = **2.20m** |
| 바닥이 2.83m (허용 상한) | 바닥 = 2.83m | 2.83 - 0.30 = **2.53m** |
| 바닥이 2.27m (허용 하한) | 바닥 = 2.27m | 2.27 - 0.30 = **1.97m** |
| 상단이 0.40m             | 상단 = 0.40m | 0.40 + 0.50 = **0.90m** |
| 상단이 0.73m (허용 상한) | 상단 = 0.73m | 0.73 + 0.50 = **1.23m** |
| 상단이 0.07m (허용 하한) | 상단 = 0.07m | 0.07 + 0.50 = **0.57m** |

→ 채점관에게 사전 통지할 내용: "센서 측정값 기준으로 2.5m 판정 범위는 1.97~2.53m, 40cm 판정 범위는 0.57~1.23m"

**조치**: 코드 변경 불필요. 플로트 조립 후 센서 위치를 자로 측정해 DOC-004 와 채점관 통지 자료에 기재.

### 1.4 시각 표시 LED 허용 (선택 항목)

원본 7페이지:

> "Companies are permitted to include **visual cues (e.g., colored LEDs or other devices)** that can be detected from the surface to signify a successful profile."

→ 색깔 LED 가 명시적 허용. 점수 직접 영향 없지만 회수 타이밍 결정에 도움.

**활용 예시**:

- 파랑: 2.5m 범위 안 (2.27~2.83m)
- 초록: 40cm 범위 안 (0.07~0.73m)
- 빨강: 범위 이탈 (재시도 필요)

ESP32 보드의 GPIO48 RGB LED 활용 가능. 얼음 1~5cm 두께면 색깔 식별 가능성 충분.

### 1.5 추가 프로파일 시도 가능

원본 7페이지:

> "If prior to recovery the company believes their float may not have maintained depth, or if the company knows their float contacted the ice or breached the surface, companies may have the float complete additional vertical profiles in an attempt to increase their score. **If the float completes an additional vertical profile that would receive a higher score, companies may use that score instead** of the penalized profile score."

**의미**: 2회로 끝이 아님. 첫 두 번이 망가져도 추가 시도 가능, 더 높은 점수로 대체.

**제한**: 한 프로파일 안에서 항목 혼용 불가. 예) "2.5m 1회차 + 40cm 3회차" 식으로 묶을 수 없음.

### 1.6 분해 / 부품 분리 금지

원본 8페이지:

> "The entire float must be less than 1 meter in length **for the entire mission**, **it cannot have multiple compartments that separate**, nor may it raise or lower any objects beyond the 1-meter limit."

→ 미션 중 부품 분리/펼침 금지. 잠수 중에 안테나가 펼쳐지거나 일부가 분리되는 설계는 안 됨.

### 1.7 Full Load Amps 측정 의무

원본 12페이지, ELEC-NRD-005:

> "Companies **MUST measure the full load amps (FLA) of their device** during waiting mode (motors off) AND during buoyancy change mode (motors on)."

→ 두 가지 모드 모두 측정 필수. DOC-004 에 기재.

| 모드                 | 측정 시점                     |
| -------------------- | ----------------------------- |
| Waiting mode         | 모터 OFF, ESP32 + 센서만 동작 |
| Buoyancy change mode | 모터 ON, 부력 변환 중         |

### 1.8 다중 배터리 팩 규칙

원본 11페이지:

> "For systems with multiple battery packs, the battery packs should be connected on the negative terminals with the fuse (5 amps max) located off of the common negative terminal connection. **Each individual battery pack should also be fused** with the properly sized fuse for that battery pack."

→ 배터리 팩이 여러 개면 각각 개별 퓨즈 + 공통 음극 단자에 메인 퓨즈. HW 팀 확인 필요.

### 1.9 한국어 문서 vs 영어 원문 — 미세 typo

원본 3페이지 (Product Demonstration Notes):

> "...ascending to a depth of **30 cm** and holding at that depth for 30 seconds..."

이후 4~6페이지의 모든 점수 정의/판정 기준은 일관되게 **40 cm** 사용.

→ 영어 원문 자체에 typo 있음. 실제 채점 기준은 40 cm. 한국어 문서는 40cm 로 정확히 기재.

---

## 2. 사람이 잠수 중 플로트를 조절해도 되는가?

### 2.1 결론

| 질문                                             | 답                                                 |
| ------------------------------------------------ | -------------------------------------------------- |
| 사람이 조절해도 되는가? (규정상)                 | **YES** — 배포 후 명령 허용                        |
| 사람이 조절해도 되는가? (물리적으로)             | **NO (잠수 중)** — RF 가 물 못 통과                |
| 잠수 중 깊이 제어를 무엇이 해야 하는가?          | **플로트 내부 자체 회로** (ESP32 또는 HW 컨트롤러) |
| 미션 핵심 (③④ 50점) 을 사람이 영향 줄 수 있는가? | **NO** — 잠수 후 회수까지 외부 개입 불가           |

### 2.2 규정 측면 — 무선 명령 자체는 허용

**금지된 것** (원본 8페이지):

> "The float must operate **independently**; it cannot be connected to the shore by a tether, and the ROV cannot interact with the float other than during recovery."

→ 테더 (전선/끈/광케이블 등 물리적 연결) + 회수 외 ROV 접촉.

**허용된 것** (원본 9페이지, DOC-004 항목):

> "**If any commands are given to the float after deployment**, those communications must also be described."

→ 배포 후 명령은 허용. 단 DOC-004 에 통신 방식 기재 의무.

**"operate independently" 의 정확한 의미**: 바로 다음 문장의 "cannot be connected to the shore by a tether" 가 부연 설명이므로, **테더로부터의 독립** 을 의미. 자율 동작 의무를 직접 명시한 건 아님.

→ 결론: 우리 시스템의 ZERO/DUMP/PING 명령은 합법.

### 2.3 물리법칙 측면 — 잠수 중에는 무선 불가

RF 신호의 수중 침투 깊이:

| 주파수                     | 수중 침투                          |
| -------------------------- | ---------------------------------- |
| WiFi / ESP-NOW (2.4 GHz)   | 수 cm 이내                         |
| RC 드론 컨트롤러 (2.4 GHz) | 수 cm 이내                         |
| Bluetooth (2.4 GHz)        | 수 cm                              |
| LoRa (433/915 MHz)         | ~30cm (담수), 더 짧음 (염수)       |
| VLF (10 kHz 이하)          | 수십 m 가능 (단, 거대 안테나 필요) |

→ 2.5m 잠수하면 모든 일반 무선이 침묵.

### 2.4 미션 시점별 사람 개입 가능성

| 시점    | 위치    | 무선 가능 | 사람 개입       |
| ------- | ------- | --------- | --------------- |
| 배포 전 | 수면 위 | YES       | YES (`Z` 키 등) |
| 잠수 중 | 수중    | NO        | NO (RF 차단)    |
| 회수 후 | 수면 위 | YES       | YES (`D` 키 등) |

→ 사람이 조절 가능한 구간은 **배포 전 + 회수 후** 뿐. 미션 점수 핵심인 ③④ (자율 프로파일 50점) 는 잠수 중이라 사람 개입 불가능.

### 2.5 잠수 중 자율 제어 — 가능한 구조 3가지

#### 구조 A: ESP32 가 모든 것을 결정 (HW 컨트롤러 통합)

```
[방수 하우징]
  ESP32 → 깊이 측정 → PID → ESC PWM → 모터
```

- 가장 간단 (보드 1개)
- ESP32 측 변경 큼 (모터 제어 / PID 추가)
- 1주일 안에는 무리

#### 구조 B: ESP32 + HW 컨트롤러 협업 (유선)

```
[방수 하우징]
  ESP32 → 깊이 측정
   ↓ GPIO/UART
  HW 컨트롤러 → PID → 모터
```

- 역할 분담 명확
- 두 보드 간 신호선 1~2개 필요
- ESP32 코드에 GPIO 출력 몇 줄만 추가
- **1주일 안에 가능**

#### 구조 C: HW 컨트롤러 단독 (자체 센서 + PID)

```
[방수 하우징]
  ESP32 → 깊이 측정 → 점수용 데이터만 기록 (독립)
  HW 컨트롤러 → 자체 압력 센서 → PID → 모터 (완전 독립)
```

- 두 보드 서로 모름
- HW 컨트롤러에 자체 압력 센서 필요
- ESP32 측 변경 0
- **HW 팀이 압력 센서 가지고 있어야 가능**

### 2.6 구조 B 의 ESP32 측 인터페이스 예시 (참고용)

가장 단순한 3-GPIO 인터페이스:

```cpp
// ESP32 측 추가 코드 (예시, 실제 적용 시 검증 필요)
#define TARGET_HIGH_PIN 4   // depth > 2.83m → 너무 깊음, 모터 위
#define TARGET_LOW_PIN  5   // depth < 2.27m → 너무 얕음, 모터 아래
#define IN_RANGE_PIN    6   // 2.27 ≤ depth ≤ 2.83 → 호버링

float depth = calibratedDepth();
digitalWrite(TARGET_HIGH_PIN, depth > 2.83);
digitalWrite(TARGET_LOW_PIN, depth < 2.27);
digitalWrite(IN_RANGE_PIN, (depth >= 2.27 && depth <= 2.83));
```

→ HW 컨트롤러는 이 3개 핀만 보고 모터 제어. PID 없이 bang-bang 으로도 ±33cm 30초 유지 가능.

**2.5m / 40cm 모드 전환** 은 시간 기반으로 ESP32 가 추가 GPIO 로 알려주거나, HW 컨트롤러가 자체 시퀀스로 결정.

---

## 3. 1주일 남은 시점에서의 결정 사항

### 3.1 HW 팀에 즉시 확인할 것

1. **자율 제어 주체**: 위의 구조 A/B/C 중 어느 쪽인가?
   - "HW 컨트롤러에 압력 센서 있음" → 구조 C
   - "센서 없음, GPIO 받을 수 있음" → 구조 B
   - "센서도 GPIO 도 없음" → 응급 상황, 구조 재검토 필요

2. **모터 위치**: 하우징 안 (시린지/임펠러) 인가, 밖 (외부 노출 프로펠러) 인가?

3. **Full Load Amps 측정값**: 대기 모드 + 부력 변환 모드 두 가지 모두 측정 가능한가? (DOC-004 필수)

### 3.2 ESP32 측 기존 코드 정합성 (이미 미션 요구사항 충족)

| 미션 요구사항                            | 코드 상태                               | 비고                |
| ---------------------------------------- | --------------------------------------- | ------------------- |
| 5초 간격 패킷                            | 구현 완료 (`PACKET_INTERVAL_MS = 5000`) | OK                  |
| 패킷 형식: 회사번호 + 시간 + 압력 + 깊이 | 구현 완료 (`formatPacket()`)            | OK                  |
| 배포 직후 1회 전송 (점수 ②)              | 구현 완료 (ZERO 직후 즉시 송출)         | OK                  |
| 회수 후 전체 데이터 전송 (점수 ⑤)        | 구현 완료 (DUMP 명령)                   | OK                  |
| 7개 연속 패킷 (0/5/10/.../30초)          | 구현 완료 (drift-free 스케줄러)         | OK                  |
| 깊이 단위 m                              | 구현 완료 ("X.XX meters")               | OK                  |
| 압력 단위 kPa                            | 구현 완료 ("XX.X kPa")                  | OK                  |
| LED 시각 표시 (선택)                     | 미구현                                  | 추가 검토 가치 있음 |
| 센서 offset 채점관 통지                  | (코드 외 절차)                          | 사전에 측정해야 함  |

### 3.3 권장 추가 작업 (1주일 안에 가능)

#### 추가 1: 깊이 센서 위치 측정 (5분, 점수 직접 영향)

플로트 조립 후 자로 측정:

- MS5837 센서 위치 (바닥에서 N cm 위, 상단에서 M cm 아래)
- DOC-004 와 채점관 통지 자료에 기재

#### 추가 2: 깊이 표시 LED (1~2시간, 회수 타이밍 보조)

ESP32 의 GPIO48 RGB LED 활용:

- 2.5m 범위 안 → 파랑
- 40cm 범위 안 → 초록
- 범위 밖 → 빨강

수면 위 운영자가 얼음 너머로 LED 색 보고 회수 타이밍 결정.

#### 추가 3: 구조 B 채택 시 ESP32 측 GPIO 출력 코드 (30분 ~ 1시간)

위 2.6 의 3-GPIO 인터페이스 추가. HW 팀과 신호 의미 사전 합의 필요.

---

## 4. 참고: 한국어 분석 문서가 정확하게 반영한 항목

원본과 일치하여 추가 검증 불필요한 항목:

- 점수 구조 총 70점 + 세부 항목 배점
- 깊이 범위 (2.5m ±33cm, 40cm ±33cm)
- 30초 유지 조건 + 7개 연속 패킷
- 패널티 -5점 (수면 돌파 / 얼음 접촉)
- 데이터 패킷 필수 항목 (회사번호 / 시간 / 압력 / 깊이)
- 회수 장치 5cm 폭/돌출 (2026 신규 요건)
- 배터리 종류 NiMH/AGM (LiPo/알카라인 불가)
- 단일 퓨즈 + 양극 5cm 이내
- 압력 해방 장치 2.5cm 이상
- 15분 시연 시간 제한
- 세계 챔피언십 특이사항 (얼음 두께 1~5cm, EGADS 용액 비중 1.025)

이 항목들은 원본 영어 규정과 일치하므로 한국어 문서를 그대로 신뢰 가능합니다.
