# ESP32-S3 N16R8 Project

ESP32-S3 DevKitC-1 N16R8 보드용 PlatformIO 프로젝트.

- **Flash**: 16 MB
- **PSRAM**: 8 MB Octal SPI
- **Framework**: Arduino

## USB 포트

보드에 USB-C 포트가 두 개 있습니다. **`USB` 라벨이 적힌 쪽을 사용하세요.** `COM` 라벨 쪽은 거의 안 씁니다.

(둘 다 전원/데이터 모두 가능하지만, 이 프로젝트의 `Serial` 출력은 `USB` 포트로만 나옵니다.)

## 자주 쓰는 명령어

### 코드 수정 후 한 번에 빌드 + 업로드 + 시리얼 모니터

```bash
pio run -t upload -t monitor
```

이 한 줄이 자동으로:

1. 코드 빌드
2. 모니터가 떠있으면 자동 닫기 (포트 점유 해제)
3. 보드에 업로드
4. 모니터 자동 재오픈

종료: `Ctrl+C`

### 그 외 명령어

| 명령                 | 용도                    |
| -------------------- | ----------------------- |
| `pio run`            | 빌드만                  |
| `pio run -t upload`  | 빌드 + 업로드           |
| `pio device monitor` | 시리얼 모니터만         |
| `pio run -t clean`   | 빌드 결과물 청소        |
| `pio device list`    | 연결된 시리얼 포트 목록 |

## 디렉토리 구조

```
.
├── platformio.ini       # 보드/빌드 설정 (N16R8 전용)
├── src/main.cpp         # 메인 코드
├── docs/                # 학습 문서
│   └── 01-learning-roadmap.md
├── include/             # 헤더 파일
├── lib/                 # 외부 라이브러리
└── test/                # 테스트 코드
```

## 참고 문서

- [학습 로드맵](docs/01-learning-roadmap.md)
- [ESP32-S3 DevKitC-1 공식 문서](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [Arduino-ESP32 API 레퍼런스](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
