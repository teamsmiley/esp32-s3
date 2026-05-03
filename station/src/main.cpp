#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LittleFS.h>

// 지상국 — float 으로부터 미션 패킷을 받고, 키 입력으로 명령을 송신.
// 수신한 모든 패킷은 LittleFS 의 received.log 에 append (E 키로만 지움).

// 우리 팀 float 보드의 MAC — 송신 대상 + 수신 화이트리스트
uint8_t FLOAT_MAC[6] = {0xAC, 0xA7, 0x04, 0xEE, 0x43, 0xB0};

static const char *RX_LOG = "/received.log";
static bool fsReady = false;

static bool isOurFloat(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != FLOAT_MAC[i]) return false;
  }
  return true;
}

void appendRxLog(const char *line) {
  if (!fsReady) return;
  File f = LittleFS.open(RX_LOG, "a");
  if (!f) {
    Serial.println("[LittleFS] open(append) 실패");
    return;
  }
  f.println(line);
  f.close();
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
  appendRxLog(buf);
}

void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.printf("  [TX %s]\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// 명령 송신 (단순 텍스트 4글자)
void sendCommand(const char *cmd) {
  Serial.printf("[CMD →] %s\n", cmd);
  esp_now_send(FLOAT_MAC, (const uint8_t *)cmd, strlen(cmd));
}

void setupLittleFS() {
  Serial.println("[LittleFS] 마운트 시도...");
  if (!LittleFS.begin(true)) {  // true = 마운트 실패 시 자동 포맷
    Serial.println("[LittleFS] 마운트 실패 — 수신 로그 비활성화");
    return;
  }
  fsReady = true;
  size_t logSize = LittleFS.exists(RX_LOG) ? LittleFS.open(RX_LOG, "r").size() : 0;
  Serial.printf("[LittleFS] 준비 완료 — %u / %u bytes 사용 (received.log: %u bytes)\n",
                LittleFS.usedBytes(), LittleFS.totalBytes(), logSize);
}

void readRxLog() {
  if (!fsReady) {
    Serial.println("[read] LittleFS 준비 안 됨");
    return;
  }
  File f = LittleFS.open(RX_LOG, "r");
  if (!f) {
    Serial.println("[read] received.log 없음");
    return;
  }
  Serial.printf("[read] 시작 — %u bytes\n", f.size());
  Serial.println("---- BEGIN received.log ----");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();
  Serial.println("---- END received.log ----");
  Serial.println("[read] 완료");
}

void eraseRxLog() {
  if (!fsReady) {
    Serial.println("[erase] LittleFS 준비 안 됨");
    return;
  }
  if (LittleFS.exists(RX_LOG)) {
    LittleFS.remove(RX_LOG);
    Serial.println("[erase] received.log 삭제 완료");
  } else {
    Serial.println("[erase] received.log 없음");
  }
}

void infoRxLog() {
  if (!fsReady) {
    Serial.println("[info] LittleFS 준비 안 됨");
    return;
  }
  size_t logSize = 0;
  if (LittleFS.exists(RX_LOG)) {
    File f = LittleFS.open(RX_LOG, "r");
    logSize = f.size();
    f.close();
  }
  Serial.printf("[info] received.log: %u bytes, FS %u / %u bytes 사용\n",
                logSize, LittleFS.usedBytes(), LittleFS.totalBytes());
}

void printHelp() {
  Serial.println("--------------------------------");
  Serial.println(" 키 입력 → float 명령:");
  Serial.println("   D = DUMP   (LittleFS 로그 dump)");
  Serial.println("   Z = ZERO   (깊이 0점 재보정)");
  Serial.println("   P = PING   (연결 확인)");
  Serial.println("   S = START  (자율 시퀀스 시작 — stub)");
  Serial.println(" 키 입력 → station 로컬:");
  Serial.println("   R = READ   (received.log 시리얼 출력)");
  Serial.println("   E = ERASE  (received.log 삭제)");
  Serial.println("   I = INFO   (파일 / FS 사용량)");
  Serial.println("   H = HELP   (이 메시지 다시 보기)");
  Serial.println("--------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== Station (지상국) ===");

  setupLittleFS();

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
      case 'R': case 'r': readRxLog(); break;
      case 'E': case 'e': eraseRxLog(); break;
      case 'I': case 'i': infoRxLog(); break;
      case 'H': case 'h': printHelp(); break;
      case '\n': case '\r': break;  // 개행은 무시
      default:
        Serial.printf("  알 수 없는 키: '%c' (H=help)\n", c);
    }
  }
}
