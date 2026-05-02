#include <Arduino.h>
#include <Wire.h>
#include "MS5837.h"

#define I2C_SDA 8
#define I2C_SCL 9

#define FLUID_DENSITY 997.0f  // 담수 kg/m^3 (바닷물은 1029.0)
#define READ_INTERVAL_MS 500

MS5837 sensor;

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

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== MS5837 깊이 센서 테스트 ===");
  Serial.printf("I2C 핀: SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA, I2C_SCL);

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
  Serial.println("Pressure(mbar) | Temp(C) | Depth(m) | Altitude(m)");
  Serial.println("---------------+---------+----------+-----------");
}

void loop() {
  sensor.read();

  Serial.printf("%13.2f | %7.2f | %8.3f | %9.2f\n",
                sensor.pressure(),
                sensor.temperature(),
                sensor.depth(),
                sensor.altitude());

  delay(READ_INTERVAL_MS);
}
