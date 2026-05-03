# 사전 요구사항

이 보드를 다룰 노트북에 무엇을 깔아야 하는지 정리한 문서입니다.

펌웨어는 이미 보드에 깔려있다고 가정합니다. 시리얼 모니터로 키 입력 (`Z` / `D`) + 회수 후 그래프 생성만 하면 됩니다.

설치할 것은 **uv 하나** 뿐입니다. PlatformIO 는 `tools/` 안의 가상환경에 자동으로 들어옵니다.

## 1. uv 설치

**macOS:**

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

**Windows (PowerShell):**

```powershell
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
```

설치 후 **새 터미널을 열어** PATH 적용. 확인:

```bash
uv --version
```

## 2. Git 으로 리포지토리 받기

- **macOS**: `xcode-select --install` (Apple 공식 CLT 안에 git 포함)
- **Windows**: [Git for Windows](https://git-scm.com/download/win)

```bash
git clone https://github.com/teamsmiley/esp32-s3.git
cd ESP32-s3
```

## 3. 가상환경 만들기 + 활성화

```bash
cd tools
uv sync                       # .venv 생성 + platformio + pyserial + matplotlib 설치

source .venv/bin/activate     # macOS / Linux
# Windows (PowerShell): .venv\Scripts\Activate.ps1
```

확인:

```bash
pio --version
```

- **shortcut**
  - macOS: `cd tools && source .venv/bin/activate`
  - Windows PowerShell: `cd tools; .venv\Scripts\Activate.ps1`

## 미션 진행 + 그래프

터미널을 두 개 사용합니다. 모니터와 그래프 도구가 같은 시리얼 포트를 점유하기 때문에 서로 분리하는 게 안정적입니다.

**터미널 1 — 모니터** (가상환경 활성화된 상태):

```bash
pio device monitor --baud 115200
# Z 키 → 깊이 0점 보정
# D 키 → 회수 후 dump
# 미션 끝나면 Ctrl+C 로 종료
```

**터미널 2 — 그래프** (새 터미널 열기):

```bash
cd tools
source .venv/bin/activate    # macOS / Linux
# Windows (PowerShell): .venv\Scripts\Activate.ps1

python read_and_graph.py
```

**키 입력 안내:**

| 키  | 언제 누르나                 | 효과                                            | 점수   |
| --- | --------------------------- | ----------------------------------------------- | ------ |
| `Z` | float 을 수면에 띄운 직후   | 깊이 0점 재보정 (`[RX] ZERO_OK ...` 응답)       | ② 5점  |
| `D` | float 회수 후 (수면 복귀)   | float 의 mission.log 전체를 station 으로 가져옴 | ⑤ 10점 |
| —   | dump 끝나면 Ctrl+C → 그래프 | `received.png` 생성                             | ⑥ 10점 |

---

## 대회 전날 점검

실제 보드를 USB 로 꽂은 상태에서 위의 "미션 진행 + 그래프" 두 터미널 흐름을 그대로 한 번 돌려봅니다.

**터미널 1 — 모니터:**

```bash
cd tools
source .venv/bin/activate    # macOS / Linux
# Windows (PowerShell): .venv\Scripts\Activate.ps1

pio device monitor --baud 115200
# [RX] PVPHSROV ... 패킷이 5초마다 흐르는지 확인
# Z 키 한 번 → [RX] ZERO_OK ... 응답 확인
# 실행
# D 키 한 번 → [RX] DUMP_OK ... 응답 확인
# 그래프 다 그려질때까지 기다림
# Ctrl+C 로 종료
```

**터미널 2 — 그래프:**

```bash
cd tools
source .venv/bin/activate    # macOS / Linux
# Windows (PowerShell): .venv\Scripts\Activate.ps1

python read_and_graph.py
# received.log + received.png 생성 확인
```

위 흐름이 다 되면 당일 그대로 사용. 막히면 아래 OS 별 함정 확인.

## OS 별 함정

### macOS

- DevKitC-1 의 USB 포트는 두 개입니다. **`USB` 라벨** 쪽에 꽂으세요. `UART` 라벨 쪽은 ROM 부트로더 메시지만 나옵니다 (자세한 내용은 `CLAUDE.md` 의 "USB 포트가 두 개" 섹션).
- 포트 인식 안 되면: `pio device list` 로 포트가 잡히는지 확인. 안 잡히면 케이블이 충전 전용일 가능성 (데이터 케이블로 교체).

### Windows

- ESP32-S3 native USB 는 Windows 10+ 에서 드라이버 없이 CDC 장치로 잡힙니다. 장치 관리자에 "알 수 없는 장치" 로 뜨면 [Espressif USB 드라이버](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) (CP210x 또는 CH343, 보드 리비전에 따라 다름) 설치.
- `cmd.exe` 말고 **Windows Terminal** 또는 **PowerShell** 사용 — `pio device monitor` 의 ANSI 색상이 정상 표시됨.
- USB-C 케이블은 반드시 **데이터 케이블**. 충전 전용은 안 됨.
