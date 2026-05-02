#include <Arduino.h>

#define LED_PIN 48
#define BUTTON_PIN 0
#define BRIGHTNESS 10
#define DEBOUNCE_MS 50

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  const char* name;
};

const Color rainbow[] = {
    {BRIGHTNESS, 0,              0,          "빨강"},
    {BRIGHTNESS, BRIGHTNESS / 2, 0,          "주황"},
    {BRIGHTNESS, BRIGHTNESS,     0,          "노랑"},
    {0,          BRIGHTNESS,     0,          "초록"},
    {0,          0,              BRIGHTNESS, "파랑"},
    {BRIGHTNESS / 3, 0,          BRIGHTNESS, "남색"},
    {BRIGHTNESS, 0,              BRIGHTNESS, "보라"},
};
const int NUM_COLORS = sizeof(rainbow) / sizeof(rainbow[0]);

int colorIndex = 0;
int lastButtonState = HIGH;
int lastReading = HIGH;
unsigned long lastChangeTime = 0;

void showColor(int idx) {
  Color c = rainbow[idx];
  neopixelWrite(LED_PIN, c.r, c.g, c.b);
  Serial.printf("[%d/%d] %s\n", idx + 1, NUM_COLORS, c.name);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== Phase 1-3: 버튼으로 색상 토글 ===");
  Serial.println("BOOT 버튼을 누를 때마다 색상이 바뀝니다");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  showColor(colorIndex);  // 초기 색상
}

void loop() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading) {
    lastChangeTime = millis();
    lastReading = reading;
  }

  if (millis() - lastChangeTime > DEBOUNCE_MS) {
    if (reading != lastButtonState) {
      lastButtonState = reading;

      // 눌림 (HIGH → LOW) 순간에만 색상 변경
      if (lastButtonState == LOW) {
        colorIndex = (colorIndex + 1) % NUM_COLORS;
        showColor(colorIndex);
      }
    }
  }
}
