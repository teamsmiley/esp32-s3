#include <Arduino.h>
#include <Wire.h>
#include "MS5837.h"

#define I2C_SDA 8
#define I2C_SCL 9
#define BOOT_BUTTON 0

#define FLUID_DENSITY 997.0f  // 담수 kg/m^3 (바닷물은 1029.0)
#define READ_INTERVAL_MS 500
#define ZERO_SAMPLES 16       // 0점 보정 시 평균낼 샘플 수
#define ZERO_SAMPLE_DELAY_MS 50

MS5837 sensor;
float depthOffset = 0.0f;     // 0점 보정 offset (m)

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

  Serial.println("BOOT 버튼을 누르면 재보정합니다 (수면에 띄운 후 누르세요).");
  Serial.println();
  Serial.println("Pressure(mbar) | Temp(C) | RawDepth(m) | Depth(m)");
  Serial.println("---------------+---------+-------------+----------");
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
    Serial.println("Pressure(mbar) | Temp(C) | RawDepth(m) | Depth(m)");
    Serial.println("---------------+---------+-------------+----------");
  }

  sensor.read();

  Serial.printf("%13.2f | %7.2f | %11.4f | %8.4f\n",
                sensor.pressure(),
                sensor.temperature(),
                sensor.depth(),
                calibratedDepth());

  delay(READ_INTERVAL_MS);
}
