#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LittleFS.h>
#include "MS5837.h"

#define LOG_FILE   "/mission.log"
#define LOG_BACKUP "/mission.log.bak"
#define DUMP_INTER_PACKET_MS 50  // gap between packets during dump (ESP-NOW queue safety)

#define I2C_SDA 8
#define I2C_SCL 9
#define BOOT_BUTTON 0

#define COMPANY_NUMBER "PVPHSROV"
#define FLUID_DENSITY 997.0f  // freshwater kg/m^3 (seawater would be 1029.0)
#define READ_INTERVAL_MS 500
#define PACKET_INTERVAL_MS 5000   // mission rule: 1 packet every 5 s
#define ZERO_SAMPLES 16       // samples to average for zero calibration
#define ZERO_SAMPLE_DELAY_MS 50

MS5837 sensor;
float depthOffset = 0.0f;     // zero-calibration offset (m)
unsigned long missionStartMs = 0;
unsigned long nextPacketMs = 0;

// Paired station board MAC. Unicast TX so other teams never receive our packets.
uint8_t stationMac[6] = {0xAC, 0xA7, 0x04, 0x13, 0x3A, 0xE8};
bool espNowReady = false;
bool fsReady = false;

// Wireless command flags (RX callback is ISR-like; heavy work runs in loop())
volatile bool cmdDumpRequested = false;
volatile bool cmdZeroRequested = false;
volatile bool cmdPingRequested = false;
volatile bool cmdStartRequested = false;

// BOOT button debounce state
int lastButtonState = HIGH;
unsigned long lastDebounceMs = 0;
const unsigned long DEBOUNCE_MS = 50;

void scanI2C() {
  Serial.println("[I2C scan start]");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found: 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("[I2C scan done — %d device(s) found]\n", found);
  if (found == 0) {
    Serial.println("  Check wiring: SDA, SCL, VCC(3.3V), GND, pull-up resistors");
  }
}

// Calibrate the current position as depth 0 m.
void calibrateZero() {
  Serial.printf("[zero calibration start — averaging %d samples]\n", ZERO_SAMPLES);
  double sum = 0.0;
  for (int i = 0; i < ZERO_SAMPLES; i++) {
    sensor.read();
    float raw = sensor.depth();
    sum += raw;
    Serial.printf("  sample %2d: %.4f m\n", i + 1, raw);
    delay(ZERO_SAMPLE_DELAY_MS);
  }
  depthOffset = (float)(sum / ZERO_SAMPLES);
  Serial.printf("[zero calibration done] offset = %.4f m\n", depthOffset);
  Serial.println();
}

// Calibrated depth (surface = 0, positive when submerged)
float calibratedDepth() {
  return sensor.depth() - depthOffset;
}

// Convert elapsed ms since boot to HH:MM:SS
void formatElapsed(unsigned long elapsedMs, char *out, size_t outLen) {
  unsigned long totalSec = elapsedMs / 1000;
  unsigned int h = totalSec / 3600;
  unsigned int m = (totalSec % 3600) / 60;
  unsigned int s = totalSec % 60;
  snprintf(out, outLen, "%02u:%02u:%02u", h, m, s);
}

// Build one mission data packet line.
// e.g. PVPHSROV 00:01:23 98.7 kPa 0.00 meters
void formatPacket(char *out, size_t outLen) {
  char timeStr[16];
  formatElapsed(millis() - missionStartMs, timeStr, sizeof(timeStr));
  float kPa = sensor.pressure() * 0.1f;  // mbar -> kPa
  float depthM = calibratedDepth();
  snprintf(out, outLen, "%s %s %.1f kPa %.2f meters",
           COMPANY_NUMBER, timeStr, kPa, depthM);
}

// ESP-NOW send-result callback (for debugging)
void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.printf("  [TX %s]\n",
                status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ESP-NOW receive callback — only handle commands from our station.
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (memcmp(mac, stationMac, 6) != 0) return;  // ignore other teams / unknown senders

  char cmd[16];
  int copyLen = len < (int)sizeof(cmd) - 1 ? len : (int)sizeof(cmd) - 1;
  memcpy(cmd, data, copyLen);
  cmd[copyLen] = '\0';

  Serial.printf("[CMD ←] %s\n", cmd);

  if      (strcmp(cmd, "DUMP") == 0) cmdDumpRequested = true;
  else if (strcmp(cmd, "ZERO") == 0) cmdZeroRequested = true;
  else if (strcmp(cmd, "PING") == 0) cmdPingRequested = true;
  else if (strcmp(cmd, "STAR") == 0) cmdStartRequested = true;
  else Serial.printf("  unknown command: %s\n", cmd);
}

// Mount LittleFS and back up the previous mission log.
void setupLittleFS() {
  Serial.println("[LittleFS] mounting...");
  if (!LittleFS.begin(true)) {  // true = auto-format on mount failure
    Serial.println("[LittleFS] mount failed");
    return;
  }
  if (LittleFS.exists(LOG_FILE)) {
    if (LittleFS.exists(LOG_BACKUP)) LittleFS.remove(LOG_BACKUP);
    LittleFS.rename(LOG_FILE, LOG_BACKUP);
    Serial.println("[LittleFS] previous mission log backed up to mission.log.bak");
  }
  fsReady = true;
  Serial.printf("[LittleFS] ready — %u / %u bytes used\n",
                LittleFS.usedBytes(), LittleFS.totalBytes());
}

// Append one line to the mission log.
void appendLog(const char *line) {
  if (!fsReady) return;
  File f = LittleFS.open(LOG_FILE, "a");
  if (!f) {
    Serial.println("[LittleFS] open(append) failed");
    return;
  }
  f.println(line);
  f.close();
}

// Stream the entire mission log over ESP-NOW (post-recovery transmission).
void dumpLog() {
  if (!fsReady) {
    Serial.println("[dump] LittleFS not ready");
    return;
  }
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) {
    Serial.println("[dump] log file not found");
    return;
  }
  Serial.println("[dump] start");
  int count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();  // strip \r etc.
    if (line.length() == 0) continue;
    Serial.printf("  [dump %d] %s\n", count + 1, line.c_str());
    if (espNowReady) {
      esp_now_send(stationMac, (const uint8_t *)line.c_str(), line.length());
    }
    delay(DUMP_INTER_PACKET_MS);  // ease ESP-NOW queue pressure
    count++;
  }
  f.close();
  Serial.printf("[dump] done — %d lines sent\n", count);
}

// ESP-NOW init (register the station as a unicast peer).
void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();   // block any attempt to join another AP

  Serial.printf("[ESP-NOW] sender MAC: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] init failed");
    return;
  }
  esp_now_register_send_cb(onEspNowSent);
  esp_now_register_recv_cb(onEspNowRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, stationMac, 6);
  peer.channel = 0;       // 0 = current STA channel
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] station peer registration failed");
    return;
  }
  espNowReady = true;
  Serial.printf("[ESP-NOW] ready — unicast -> %02X:%02X:%02X:%02X:%02X:%02X\n",
                stationMac[0], stationMac[1], stationMac[2],
                stationMac[3], stationMac[4], stationMac[5]);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== MS5837 depth sensor + zero calibration ===");
  Serial.printf("I2C pins: SDA=GPIO%d, SCL=GPIO%d\n", I2C_SDA, I2C_SCL);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL);
  scanI2C();

  Serial.println("[MS5837 init attempt...]");
  while (!sensor.init()) {
    Serial.println("  init failed — check wiring and sensor model. Retrying in 5 s.");
    delay(5000);
  }
  Serial.println("[MS5837 init OK]");

  sensor.setModel(MS5837::MS5837_30BA);
  sensor.setFluidDensity(FLUID_DENSITY);
  Serial.printf("Model: MS5837-30BA, fluid density: %.1f kg/m^3\n", FLUID_DENSITY);
  Serial.println();

  calibrateZero();
  setupLittleFS();
  setupEspNow();

  Serial.println("Press BOOT button to recalibrate (do this after placing on the surface).");
  Serial.println("Press 'D' on the serial console to dump the log file to the station.");
  Serial.printf("Company number: %s, packet interval: %d ms\n", COMPANY_NUMBER, PACKET_INTERVAL_MS);
  Serial.println();

  missionStartMs = millis();
  nextPacketMs = missionStartMs;  // first packet fires immediately
}

// True once per press (debounced).
bool bootPressed() {
  int reading = digitalRead(BOOT_BUTTON);
  if (reading != lastButtonState) {
    lastDebounceMs = millis();
    lastButtonState = reading;
  }
  if (reading == LOW && (millis() - lastDebounceMs) > DEBOUNCE_MS) {
    // Block until release so it doesn't retrigger.
    while (digitalRead(BOOT_BUTTON) == LOW) delay(10);
    lastButtonState = HIGH;
    return true;
  }
  return false;
}

void loop() {
  if (bootPressed()) {
    Serial.println("\n[BOOT button — recalibrating]");
    calibrateZero();
    missionStartMs = millis();   // mission clock restarts at calibration
    nextPacketMs = missionStartMs;
  }

  // Serial command handling (dev convenience)
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'D' || c == 'd') dumpLog();
  }

  // Wireless command flags (set in callback, executed here)
  if (cmdDumpRequested) {
    cmdDumpRequested = false;
    dumpLog();
  }
  if (cmdZeroRequested) {
    cmdZeroRequested = false;
    Serial.println("[CMD] ZERO handling start");
    calibrateZero();
    missionStartMs = millis();
    nextPacketMs = missionStartMs;
    char resp[80];
    snprintf(resp, sizeof(resp), "ZERO_OK offset=%.4f m", depthOffset);
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)resp, strlen(resp));
  }
  if (cmdPingRequested) {
    cmdPingRequested = false;
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)"PONG", 4);
    Serial.println("[CMD] PING -> PONG reply");
  }
  if (cmdStartRequested) {
    cmdStartRequested = false;
    Serial.println("[CMD] START — autonomous sequence not implemented yet (stub)");
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)"START_STUB", 10);
  }

  sensor.read();

  // Emit a mission data packet every 5 s (serial + wireless + LittleFS)
  if ((long)(millis() - nextPacketMs) >= 0) {
    char packet[80];
    formatPacket(packet, sizeof(packet));
    Serial.println(packet);
    if (espNowReady) {
      esp_now_send(stationMac, (uint8_t *)packet, strlen(packet));
    }
    appendLog(packet);
    nextPacketMs += PACKET_INTERVAL_MS;
  }

  delay(READ_INTERVAL_MS);
}
