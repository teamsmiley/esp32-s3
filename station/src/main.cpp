#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// 지상국 수신기 — float 보드에서 broadcast 한 미션 데이터 패킷을 받아 시리얼로 출력.

// 패킷 수신 콜백
// recv_info->src_addr 에 송신자 MAC 이 들어 있음
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  // 받은 데이터는 NULL 종료가 안 돼있을 수 있으므로 안전하게 복사
  char buf[128];
  int copyLen = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
  memcpy(buf, data, copyLen);
  buf[copyLen] = '\0';

  const uint8_t *m = info->src_addr;
  Serial.printf("[RX %02X:%02X:%02X:%02X:%02X:%02X] %s\n",
                m[0], m[1], m[2], m[3], m[4], m[5], buf);
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
  Serial.println("[ESP-NOW] 수신 대기 중 (broadcast)");
  Serial.println();
}

void loop() {
  // 모든 동작은 ESP-NOW 콜백에서 처리. 여기선 할 일 없음.
  delay(1000);
}
