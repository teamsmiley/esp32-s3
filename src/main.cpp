#include <Arduino.h>

#define LED_PIN 48          // RGB LED
#define BUTTON_PIN 0        // BOOT 버튼 (보드 내장)
#define DEBOUNCE_MS 50      // 디바운스 시간 (밀리초)

int lastButtonState = HIGH;        // 직전 안정 상태 (HIGH = 안 눌림)
int lastReading = HIGH;            // 직전 raw 읽기값
unsigned long lastChangeTime = 0;  // 마지막으로 raw 값이 바뀐 시각
int pressCount = 0;                // 눌린 횟수

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== Phase 1-2: BOOT 버튼 입력 ===");
  Serial.println("BOOT 버튼을 눌러보세요");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  neopixelWrite(LED_PIN, 5, 0, 0);  // 어두운 빨강으로 켜둠
}

void loop() {
  int reading = digitalRead(BUTTON_PIN);

  // raw 값이 바뀐 순간을 기록
  if (reading != lastReading) {
    lastChangeTime = millis();
    lastReading = reading;
  }

  // 디바운스: 변화 후 일정 시간 안정되어야 진짜 눌림으로 간주
  if (millis() - lastChangeTime > DEBOUNCE_MS) {
    if (reading != lastButtonState) {
      lastButtonState = reading;

      if (lastButtonState == LOW) {
        // 눌림 (HIGH → LOW)
        pressCount++;
        Serial.printf("[누름 #%d] %lu ms\n", pressCount, millis());
      } else {
        // 떼임 (LOW → HIGH)
        Serial.println("  └── 뗌");
      }
    }
  }
}
