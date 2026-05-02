#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LittleFS.h>
#include "MS5837.h"

#define LOG_FILE   "/mission.log"
#define LOG_BACKUP "/mission.log.bak"
#define DUMP_INTER_PACKET_MS 50  // dump 시 패킷 간 간격 (ESP-NOW 큐 보호)

#define I2C_SDA 8
#define I2C_SCL 9
#define BOOT_BUTTON 0

#define COMPANY_NUMBER "PVPHSROV"
#define FLUID_DENSITY 997.0f  // 담수 kg/m^3 (바닷물은 1029.0)
#define READ_INTERVAL_MS 500
#define PACKET_INTERVAL_MS 5000   // 미션 규정: 5초마다 패킷 1개
#define ZERO_SAMPLES 16       // 0점 보정 시 평균낼 샘플 수
#define ZERO_SAMPLE_DELAY_MS 50

MS5837 sensor;
float depthOffset = 0.0f;     // 0점 보정 offset (m)
unsigned long missionStartMs = 0;
unsigned long nextPacketMs = 0;

// 짝 station 보드의 MAC. unicast 송신 → 다른 팀에게 패킷이 도달하지 않음.
uint8_t stationMac[6] = {0xAC, 0xA7, 0x04, 0x13, 0x3A, 0xE8};
bool espNowReady = false;
bool fsReady = false;

// BOOT 버튼 디바운스 상태
int lastButtonState = HIGH;
unsigned long lastDebounceMs = 0;
const unsigned long DEBOUNCE_MS = 50;

void scanI2C() {
  Serial.println("[I2C 스캔 시작]");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  발견: 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("[I2C 스캔 완료 — %d 개 장치 발견]\n", found);
  if (found == 0) {
    Serial.println("  배선 확인: SDA, SCL, VCC(3.3V), GND, 풀업 저항");
  }
}

// 현재 위치를 깊이 0m 으로 보정
void calibrateZero() {
  Serial.printf("[0점 보정 시작 — %d회 평균]\n", ZERO_SAMPLES);
  double sum = 0.0;
  for (int i = 0; i < ZERO_SAMPLES; i++) {
    sensor.read();
    float raw = sensor.depth();
    sum += raw;
    Serial.printf("  샘플 %2d: %.4f m\n", i + 1, raw);
    delay(ZERO_SAMPLE_DELAY_MS);
  }
  depthOffset = (float)(sum / ZERO_SAMPLES);
  Serial.printf("[0점 보정 완료] offset = %.4f m\n", depthOffset);
  Serial.println();
}

// 보정된 깊이값 (수면이 0, 잠수 시 양수)
float calibratedDepth() {
  return sensor.depth() - depthOffset;
}

// 부팅 후 경과 시간을 HH:MM:SS 로 변환
void formatElapsed(unsigned long elapsedMs, char *out, size_t outLen) {
  unsigned long totalSec = elapsedMs / 1000;
  unsigned int h = totalSec / 3600;
  unsigned int m = (totalSec % 3600) / 60;
  unsigned int s = totalSec % 60;
  snprintf(out, outLen, "%02u:%02u:%02u", h, m, s);
}

// 미션 데이터 패킷 한 줄 생성
// 예: PVPHSROV 00:01:23 98.7 kPa 0.00 meters
void formatPacket(char *out, size_t outLen) {
  char timeStr[16];
  formatElapsed(millis() - missionStartMs, timeStr, sizeof(timeStr));
  float kPa = sensor.pressure() * 0.1f;  // mbar -> kPa
  float depthM = calibratedDepth();
  snprintf(out, outLen, "%s %s %.1f kPa %.2f meters",
           COMPANY_NUMBER, timeStr, kPa, depthM);
}

// ESP-NOW 송신 결과 콜백 (디버깅용)
void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.printf("  [TX %s]\n",
                status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// LittleFS 마운트 + 직전 미션 로그 백업
void setupLittleFS() {
  Serial.println("[LittleFS] 마운트 시도...");
  if (!LittleFS.begin(true)) {  // true = 마운트 실패 시 자동 포맷
    Serial.println("[LittleFS] 마운트 실패");
    return;
  }
  if (LittleFS.exists(LOG_FILE)) {
    if (LittleFS.exists(LOG_BACKUP)) LittleFS.remove(LOG_BACKUP);
    LittleFS.rename(LOG_FILE, LOG_BACKUP);
    Serial.println("[LittleFS] 직전 미션 로그를 mission.log.bak 으로 백업");
  }
  fsReady = true;
  Serial.printf("[LittleFS] 준비 완료 — %u / %u bytes 사용\n",
                LittleFS.usedBytes(), LittleFS.totalBytes());
}

// 미션 로그에 한 줄 append
void appendLog(const char *line) {
  if (!fsReady) return;
  File f = LittleFS.open(LOG_FILE, "a");
  if (!f) {
    Serial.println("[LittleFS] open(append) 실패");
    return;
  }
  f.println(line);
  f.close();
}

// 미션 로그 전체를 ESP-NOW 로 송출 (회수 후 데이터 전송 시뮬레이션)
void dumpLog() {
  if (!fsReady) {
    Serial.println("[dump] LittleFS 준비 안 됨");
    return;
  }
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) {
    Serial.println("[dump] 로그 파일 없음");
    return;
  }
  Serial.println("[dump] 시작");
  int count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();  // \r 등 제거
    if (line.length() == 0) continue;
    Serial.printf("  [dump %d] %s\n", count + 1, line.c_str());
    if (espNowReady) {
      esp_now_send(stationMac, (const uint8_t *)line.c_str(), line.length());
    }
    delay(DUMP_INTER_PACKET_MS);  // ESP-NOW 큐 부담 감소
    count++;
  }
  f.close();
  Serial.printf("[dump] 완료 — %d 줄 송신\n", count);
}

// ESP-NOW 초기화 (station unicast peer 등록)
void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();   // 다른 AP 에 붙으려는 시도 차단

  Serial.printf("[ESP-NOW] 송신측 MAC: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] init 실패");
    return;
  }
  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, stationMac, 6);
  peer.channel = 0;       // 0 = 현재 STA 채널
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] station peer 등록 실패");
    return;
  }
  espNowReady = true;
  Serial.printf("[ESP-NOW] 준비 완료 — unicast → %02X:%02X:%02X:%02X:%02X:%02X\n",
                stationMac[0], stationMac[1], stationMac[2],
                stationMac[3], stationMac[4], stationMac[5]);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== MS5837 깊이 센서 + 0점 보정 ===");
  Serial.printf("I2C 핀: SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA, I2C_SCL);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL);
  scanI2C();

  Serial.println("[MS5837 초기화 시도...]");
  while (!sensor.init()) {
    Serial.println("  초기화 실패 — 배선과 센서 모델을 확인하세요. 5초 후 재시도.");
    delay(5000);
  }
  Serial.println("[MS5837 초기화 성공]");

  sensor.setModel(MS5837::MS5837_30BA);
  sensor.setFluidDensity(FLUID_DENSITY);
  Serial.printf("모델: MS5837-30BA, 유체 밀도: %.1f kg/m^3\n", FLUID_DENSITY);
  Serial.println();

  calibrateZero();
  setupLittleFS();
  setupEspNow();

  Serial.println("BOOT 버튼을 누르면 재보정합니다 (수면에 띄운 후 누르세요).");
  Serial.println("시리얼에 'D' 입력 시 로그 파일을 station 에 dump 합니다.");
  Serial.printf("회사번호: %s, 패킷 주기: %d ms\n", COMPANY_NUMBER, PACKET_INTERVAL_MS);
  Serial.println();

  missionStartMs = millis();
  nextPacketMs = missionStartMs;  // 첫 패킷은 즉시
}

// BOOT 버튼이 눌렸는지 (디바운스 적용, 누른 순간 1회)
bool bootPressed() {
  int reading = digitalRead(BOOT_BUTTON);
  if (reading != lastButtonState) {
    lastDebounceMs = millis();
    lastButtonState = reading;
  }
  if (reading == LOW && (millis() - lastDebounceMs) > DEBOUNCE_MS) {
    // 한 번 처리하면 release까지 다시 안 트리거되도록
    while (digitalRead(BOOT_BUTTON) == LOW) delay(10);
    lastButtonState = HIGH;
    return true;
  }
  return false;
}

void loop() {
  if (bootPressed()) {
    Serial.println("\n[BOOT 버튼 감지 — 재보정]");
    calibrateZero();
    missionStartMs = millis();   // 보정 직후를 미션 시작으로 재설정
    nextPacketMs = missionStartMs;
  }

  // 시리얼 명령 처리
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'D' || c == 'd') {
      dumpLog();
    }
  }

  sensor.read();

  // 5초마다 미션 데이터 패킷 송출 (시리얼 + 무선 + LittleFS)
  if ((long)(millis() - nextPacketMs) >= 0) {
    char packet[80];
    formatPacket(packet, sizeof(packet));
    Serial.println(packet);
    if (espNowReady) {
      esp_now_send(stationMac, (uint8_t *)packet, strlen(packet));
    }
    appendLog(packet);
    nextPacketMs += PACKET_INTERVAL_MS;
  }

  delay(READ_INTERVAL_MS);
}
