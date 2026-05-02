#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// 지상국 — float 으로부터 미션 패킷을 받고, 키 입력으로 명령을 송신.

// 우리 팀 float 보드의 MAC — 송신 대상 + 수신 화이트리스트
uint8_t FLOAT_MAC[6] = {0xAC, 0xA7, 0x04, 0xEE, 0x43, 0xB0};

static bool isOurFloat(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != FLOAT_MAC[i]) return false;
  }
  return true;
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!isOurFloat(mac)) {
    Serial.printf("[ignored %02X:%02X:%02X:%02X:%02X:%02X] (다른 팀 패킷)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return;
  }
  char buf[128];
  int copyLen = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
  memcpy(buf, data, copyLen);
  buf[copyLen] = '\0';
  Serial.printf("[RX] %s\n", buf);
}

void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.printf("  [TX %s]\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// 명령 송신 (단순 텍스트 4글자)
void sendCommand(const char *cmd) {
  Serial.printf("[CMD →] %s\n", cmd);
  esp_now_send(FLOAT_MAC, (const uint8_t *)cmd, strlen(cmd));
}

void printHelp() {
  Serial.println("--------------------------------");
  Serial.println(" 키 입력 → float 명령:");
  Serial.println("   D = DUMP   (LittleFS 로그 dump)");
  Serial.println("   Z = ZERO   (깊이 0점 재보정)");
  Serial.println("   P = PING   (연결 확인)");
  Serial.println("   S = START  (자율 시퀀스 시작 — stub)");
  Serial.println("   H = HELP   (이 메시지 다시 보기)");
  Serial.println("--------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== Station (지상국) ===");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("[ESP-NOW] station MAC: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] init 실패 — 재부팅하세요");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_register_send_cb(onEspNowSent);

  // float peer 등록 (송신용)
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, FLOAT_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] float peer 등록 실패");
  }

  Serial.printf("[ESP-NOW] float MAC %02X:%02X:%02X:%02X:%02X:%02X 양방향 통신 준비\n",
                FLOAT_MAC[0], FLOAT_MAC[1], FLOAT_MAC[2],
                FLOAT_MAC[3], FLOAT_MAC[4], FLOAT_MAC[5]);
  Serial.println();
  printHelp();
  Serial.println();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'D': case 'd': sendCommand("DUMP"); break;
      case 'Z': case 'z': sendCommand("ZERO"); break;
      case 'P': case 'p': sendCommand("PING"); break;
      case 'S': case 's': sendCommand("STAR"); break;  // 4 byte 명령용 줄임
      case 'H': case 'h': printHelp(); break;
      case '\n': case '\r': break;  // 개행은 무시
      default:
        Serial.printf("  알 수 없는 키: '%c' (H=help)\n", c);
    }
  }
}
