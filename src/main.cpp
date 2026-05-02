#include <Arduino.h>

#define LED_PIN 48    // ESP32-S3 DevKitC-1 의 RGB LED 가 연결된 핀
#define BRIGHTNESS 10 // 밝기 (0~255). 너무 밝으면 눈 아프니 낮게.

// 무지개 7색 (R, G, B 값)
struct Color
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
  const char *name;
};

const Color rainbow[] = {
    {BRIGHTNESS, 0, 0, "빨강"},
    {BRIGHTNESS, BRIGHTNESS / 2, 0, "주황"},
    {BRIGHTNESS, BRIGHTNESS, 0, "노랑"},
    {0, BRIGHTNESS, 0, "초록"},
    {0, 0, BRIGHTNESS, "파랑"},
    {BRIGHTNESS / 3, 0, BRIGHTNESS, "남색"},
    {BRIGHTNESS, 0, BRIGHTNESS, "보라"},
};

const int NUM_COLORS = sizeof(rainbow) / sizeof(rainbow[0]);

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== Phase 1-1: 무지개 LED 시작 ===");
}

void loop()
{
  for (int i = 0; i < NUM_COLORS; i++)
  {
    Color c = rainbow[i];
    neopixelWrite(LED_PIN, c.r, c.g, c.b);
    Serial.printf("[%d/%d] %s (R=%d, G=%d, B=%d)\n",
                  i + 1, NUM_COLORS, c.name, c.r, c.g, c.b);
    delay(1000);
  }
}
