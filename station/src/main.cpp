#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// 지상국 수신기 — 우리 float 보드에서 unicast 한 미션 데이터 패킷만 받아 시리얼로 출력.

// 우리 팀 float 보드의 MAC — 다른 팀 패킷은 모두 무시
const uint8_t FLOAT_MAC[6] = {0xAC, 0xA7, 0x04, 0xEE, 0x43, 0xB0};

static bool isOurFloat(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != FLOAT_MAC[i]) return false;
  }
  return true;
}

// 패킷 수신 콜백 (Arduino-ESP32 v2.x 시그니처: 첫 인자 = 송신자 MAC)
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!isOurFloat(mac)) {
    // 다른 팀 패킷 — 디버깅용으로만 한 번 표시하고 흘려보냄
    Serial.printf("[ignored %02X:%02X:%02X:%02X:%02X:%02X] (다른 팀 패킷)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return;
  }

  // 받은 데이터는 NULL 종료가 안 돼있을 수 있으므로 안전하게 복사
  char buf[128];
  int copyLen = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
  memcpy(buf, data, copyLen);
  buf[copyLen] = '\0';

  Serial.printf("[RX] %s\n", buf);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== Station (지상국 수신기) ===");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("[ESP-NOW] 수신측 MAC: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] init 실패 — 재부팅하세요");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onEspNowRecv);
  Serial.printf("[ESP-NOW] 수신 대기 중 — float MAC %02X:%02X:%02X:%02X:%02X:%02X 만 통과\n",
                FLOAT_MAC[0], FLOAT_MAC[1], FLOAT_MAC[2],
                FLOAT_MAC[3], FLOAT_MAC[4], FLOAT_MAC[5]);
  Serial.println();
}

void loop() {
  // 모든 동작은 ESP-NOW 콜백에서 처리. 여기선 할 일 없음.
  delay(1000);
}
