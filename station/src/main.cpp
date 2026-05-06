#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LittleFS.h>

// Ground station — receives mission packets from the float and sends commands via key input.
// All received packets are appended to received.log on LittleFS (only erased by 'E' key).

// Our float board's MAC — TX target + RX whitelist
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
    Serial.println("[LittleFS] open(append) failed");
    return;
  }
  f.println(line);
  f.close();
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (!isOurFloat(mac)) {
    Serial.printf("[ignored %02X:%02X:%02X:%02X:%02X:%02X] (other team's packet)\n",
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

// Send a command (simple 4-byte text)
void sendCommand(const char *cmd) {
  Serial.printf("[CMD →] %s\n", cmd);
  esp_now_send(FLOAT_MAC, (const uint8_t *)cmd, strlen(cmd));
}

void setupLittleFS() {
  Serial.println("[LittleFS] mounting...");
  if (!LittleFS.begin(true)) {  // true = auto-format on mount failure
    Serial.println("[LittleFS] mount failed — RX log disabled");
    return;
  }
  fsReady = true;
  size_t logSize = LittleFS.exists(RX_LOG) ? LittleFS.open(RX_LOG, "r").size() : 0;
  Serial.printf("[LittleFS] ready — %u / %u bytes used (received.log: %u bytes)\n",
                LittleFS.usedBytes(), LittleFS.totalBytes(), logSize);
}

void readRxLog() {
  if (!fsReady) {
    Serial.println("[read] LittleFS not ready");
    return;
  }
  File f = LittleFS.open(RX_LOG, "r");
  if (!f) {
    Serial.println("[read] received.log not found");
    return;
  }
  Serial.printf("[read] start — %u bytes\n", f.size());
  Serial.println("---- BEGIN received.log ----");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();
  Serial.println("---- END received.log ----");
  Serial.println("[read] done");
}

void eraseRxLog() {
  if (!fsReady) {
    Serial.println("[erase] LittleFS not ready");
    return;
  }
  if (LittleFS.exists(RX_LOG)) {
    LittleFS.remove(RX_LOG);
    Serial.println("[erase] received.log deleted");
  } else {
    Serial.println("[erase] received.log not found");
  }
}

void infoRxLog() {
  if (!fsReady) {
    Serial.println("[info] LittleFS not ready");
    return;
  }
  size_t logSize = 0;
  if (LittleFS.exists(RX_LOG)) {
    File f = LittleFS.open(RX_LOG, "r");
    logSize = f.size();
    f.close();
  }
  Serial.printf("[info] received.log: %u bytes, FS %u / %u bytes used\n",
                logSize, LittleFS.usedBytes(), LittleFS.totalBytes());
}

void printHelp() {
  Serial.println("--------------------------------");
  Serial.println(" Keys -> float commands:");
  Serial.println("   S = START  (run 3-profile mission)");
  Serial.println("   X = ABORT  (stop motor, return to idle)");
  Serial.println("   T = TEST   (10s speed ramp self-test)");
  Serial.println("   C = CALI   (in-water HOLD-PWM auto-calibration → /cali.txt)");
  Serial.println("   Z = ZERO   (recalibrate depth zero)");
  Serial.println("   P = PING   (connection check)");
  Serial.println("   D = DUMP   (dump LittleFS log)");
  Serial.println(" Bench-test (forwarded to float, no water needed):");
  Serial.println("   F = toggle fake-depth mode");
  Serial.println("   0 = fake depth 0.00 m  (surface)");
  Serial.println("   2 = fake depth 2.50 m  (deep target)");
  Serial.println("   4 = fake depth 0.70 m  (shallow target, bottom-ref)");
  Serial.println("   + = fake depth +0.10 m");
  Serial.println("   - = fake depth -0.10 m");
  Serial.println(" Keys -> station local:");
  Serial.println("   R = READ   (print received.log to serial)");
  Serial.println("   E = ERASE  (delete received.log)");
  Serial.println("   I = INFO   (file / FS usage)");
  Serial.println("   H = HELP   (show this message again)");
  Serial.println("--------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== Station (ground station) ===");

  setupLittleFS();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("[ESP-NOW] station MAC: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] init failed — please reboot");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_register_send_cb(onEspNowSent);

  // Register float peer (for TX)
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, FLOAT_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] float peer registration failed");
  }

  Serial.printf("[ESP-NOW] bidirectional comm ready with float MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
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
      case 'S': case 's': sendCommand("STAR"); break;  // shortened to 4 bytes
      case 'X': case 'x': sendCommand("ABRT"); break;
      case 'T': case 't': sendCommand("TEST"); break;
      case 'C': case 'c': sendCommand("CALI"); break;
      // Fake-depth bench-test commands (forwarded to float)
      case 'F': case 'f': sendCommand("FAKE"); break;  // toggle fake mode
      case '0':           sendCommand("FK0M"); break;  // fake = 0.00 m
      case '2':           sendCommand("FK2M"); break;  // fake = 2.50 m (deep preset)
      case '4':           sendCommand("FK4M"); break;  // fake = 0.70 m (shallow preset)
      case '+': case '=': sendCommand("FKUP"); break;  // fake +0.10 m
      case '-': case '_': sendCommand("FKDN"); break;  // fake -0.10 m
      case 'R': case 'r': readRxLog(); break;
      case 'E': case 'e': eraseRxLog(); break;
      case 'I': case 'i': infoRxLog(); break;
      case 'H': case 'h': printHelp(); break;
      case '\n': case '\r': break;  // ignore newlines
      default:
        Serial.printf("  unknown key: '%c' (H=help)\n", c);
    }
  }
}
