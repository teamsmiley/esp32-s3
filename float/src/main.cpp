#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LittleFS.h>

// Set to 0 to skip MS5837 init/reads (debug ESP-NOW + motor without the sensor wired).
#define USE_DEPTH_SENSOR 1

#if USE_DEPTH_SENSOR
#include "MS5837.h"
#endif

#define LOG_FILE   "/mission.log"
#define LOG_BACKUP "/mission.log.bak"
#define DUMP_INTER_PACKET_MS 50  // gap between packets during dump (ESP-NOW queue safety)

#define I2C_SDA 8
#define I2C_SCL 9
#define BOOT_BUTTON 0

// L298N Motor B (OUT3/OUT4) with PWM speed control on ENB.
// One-way buoyancy engine: pump only INTAKES water (sink). Motor OFF lets the float
// passively rise via residual positive buoyancy. Reverse is wired but unused by the mission.
// IN3=HIGH, IN4=LOW = descent (intake).   IN3=LOW, IN4=LOW = stop (rise naturally).
// ENB jumper REMOVED — GPIO4 drives ENB with PWM (0=stop, 255=full).
#define MOTOR_IN3 17
#define MOTOR_IN4 18
#define MOTOR_ENB 4

#define MOTOR_SPEED_FULL          255   // DESCEND legs (full intake)
#define MOTOR_SPEED_HOLD_DEFAULT   90   // fallback if /cali.txt absent (~35% duty)
int motorSpeedHold = MOTOR_SPEED_HOLD_DEFAULT;   // overridden at boot from /cali.txt
#define CALI_FILE "/cali.txt"

// Float geometry. Sensor is at the MIDPOINT of a 12-inch (30.48 cm) tall float (6 in from bottom).
// All mission depth values below are expressed as the float BOTTOM's depth.
// Mid-mounting is preferred over top-mounting because the MS5837 saturates at ~0 m once it
// breaches air; mid keeps the sensor submerged across the entire HOLD_SHALLOW band, giving
// continuous depth signal for surface-breach prevention.
#define FLOAT_HEIGHT_M            0.3048f   // 12 in
#define SENSOR_OFFSET_FROM_BOTTOM 0.1524f   //  6 in (sensor->bottom distance)

// Mission profile parameters (MATE Floats 2026), all bottom-referenced.
// Deep band:    bottom 2.27 ~ 2.83 m  (rule states "2.5m at the bottom, ±33 cm")
// Shallow band: top    0.07 ~ 0.73 m  -> bottom = top + FLOAT_HEIGHT_M
#define DEEP_MIN_M       2.27f
#define DEEP_MAX_M       2.83f
#define SHALLOW_MIN_M    (0.07f + FLOAT_HEIGHT_M)  // 0.3748 m
#define SHALLOW_MAX_M    (0.73f + FLOAT_HEIGHT_M)  // 1.0348 m
#define HOLD_DURATION_MS 30000UL
// Bottom-referenced surface threshold. With the sensor at the midpoint (6 in above bottom),
// the sensor stays submerged when the top is at the surface — so reportedDepth() drops to
// roughly half the float height when the float reaches its natural floating position.
#define SURFACE_M        0.20f
#define PROFILE_COUNT    3
#define MAX_DEPTH_M      3.20f       // emergency stop threshold (bottom)
#define STATE_TIMEOUT_MS 90000UL     // per-state watchdog (descend/ascend)

#define COMPANY_NUMBER "PVPHSROV"
#define FLUID_DENSITY 997.0f  // freshwater kg/m^3 (seawater would be 1029.0)
#define READ_INTERVAL_MS 500
#define PACKET_INTERVAL_MS 5000   // mission rule: 1 packet every 5 s
#define ZERO_SAMPLES 16       // samples to average for zero calibration
#define ZERO_SAMPLE_DELAY_MS 50

#if USE_DEPTH_SENSOR
MS5837 sensor;
#endif
float depthOffset = 0.0f;     // zero-calibration offset (m)
unsigned long missionStartMs = 0;
unsigned long nextPacketMs = 0;

// Paired station board MAC. Unicast TX so other teams never receive our packets.
uint8_t stationMac[6] = {0xAC, 0xA7, 0x04, 0x13, 0x3A, 0xE8};
bool espNowReady = false;
bool fsReady = false;

// Wireless command flags (RX callback is ISR-like; heavy work runs in loop())
volatile bool  cmdDumpRequested       = false;
volatile bool  cmdZeroRequested       = false;
volatile bool  cmdPingRequested       = false;
volatile bool  cmdStartRequested      = false;
volatile bool  cmdAbortRequested      = false;
volatile bool  cmdTestRequested       = false;
volatile bool  cmdFakeToggleRequested = false;
volatile float cmdFakeSetValue        = -1.0f;   // -1 = no pending set
volatile float cmdFakeDelta           = 0.0f;    // 0 = no pending delta
volatile bool  cmdCaliRequested       = false;

// BOOT button debounce state
int lastButtonState = HIGH;
unsigned long lastDebounceMs = 0;
const unsigned long DEBOUNCE_MS = 50;

// Motor direction + speed. We track last (dir, speed) to suppress redundant log spam.
enum MotorDir { MOTOR_OFF, MOTOR_DESCEND, MOTOR_ASCEND };
MotorDir motorDir = MOTOR_OFF;
int motorSpeed = 0;

void motorSetSpeed(MotorDir dir, int speed) {
  if (speed < 0) speed = 0;
  if (speed > 255) speed = 255;

  bool changed = (dir != motorDir) || (speed != motorSpeed);

  switch (dir) {
    case MOTOR_DESCEND:
      digitalWrite(MOTOR_IN3, HIGH);
      digitalWrite(MOTOR_IN4, LOW);
      analogWrite(MOTOR_ENB, speed);
      break;
    case MOTOR_ASCEND:
      digitalWrite(MOTOR_IN3, LOW);
      digitalWrite(MOTOR_IN4, HIGH);
      analogWrite(MOTOR_ENB, speed);
      break;
    case MOTOR_OFF:
    default:
      digitalWrite(MOTOR_IN3, LOW);
      digitalWrite(MOTOR_IN4, LOW);
      analogWrite(MOTOR_ENB, 0);
      speed = 0;
      break;
  }

  // Only log on direction change to keep ramp test output readable.
  if (changed && dir != motorDir) {
    const char *name = dir == MOTOR_DESCEND ? "DESCEND (intake)"
                     : dir == MOTOR_ASCEND  ? "ASCEND (eject)"
                                            : "OFF";
    Serial.printf("[MOTOR] %s (speed=%d)\n", name, speed);
  }

  motorDir = dir;
  motorSpeed = speed;
}

// Convenience wrappers (full-speed by default for legacy call sites)
void motorSet(MotorDir dir) { motorSetSpeed(dir, MOTOR_SPEED_FULL); }
void motorStop() { motorSetSpeed(MOTOR_OFF, 0); }

// Mission state machine
enum MissionState {
  MS_IDLE,             // surface, awaiting trigger
  MS_DESCEND,          // motor intake until depth >= DEEP_MIN_M
  MS_HOLD_DEEP,        // bang-bang at 2.5m for 30s
  MS_ASCEND_SHALLOW,   // motor eject until depth <= SHALLOW_MAX_M
  MS_HOLD_SHALLOW,     // bang-bang at 40cm for 30s
  MS_SURFACE,          // motor eject until depth <= SURFACE_M
  MS_DONE              // all profiles complete
};
MissionState missionState = MS_IDLE;
int profileIndex = 0;          // 0..PROFILE_COUNT-1
unsigned long stateStartMs = 0;
unsigned long holdStartMs = 0;  // restarts whenever depth leaves the band

const char *stateName(MissionState s) {
  switch (s) {
    case MS_IDLE:           return "IDLE";
    case MS_DESCEND:        return "DESCEND";
    case MS_HOLD_DEEP:      return "HOLD_DEEP";
    case MS_ASCEND_SHALLOW: return "ASCEND_SHALLOW";
    case MS_HOLD_SHALLOW:   return "HOLD_SHALLOW";
    case MS_SURFACE:        return "SURFACE";
    case MS_DONE:           return "DONE";
  }
  return "?";
}

void enterState(MissionState s) {
  missionState = s;
  stateStartMs = millis();
  holdStartMs = 0;
  Serial.printf("[STATE] -> %s (profile %d/%d)\n",
                stateName(s), profileIndex + 1, PROFILE_COUNT);
}

void missionStart() {
  profileIndex = 0;
  enterState(MS_DESCEND);
}

// Speed ramp self-test: 0 -> 255 over 5 s (descend dir), then 255 -> 0 over 5 s. Total 10 s.
// Lets you watch the motor accelerate/decelerate to verify PWM wiring on ENB.
#define RAMP_TEST_DURATION_MS 10000UL
bool rampTestActive = false;
unsigned long rampTestStartMs = 0;
unsigned long rampTestLastLogMs = 0;

void rampTestStart() {
  rampTestActive = true;
  rampTestStartMs = millis();
  rampTestLastLogMs = 0;
  Serial.println("[TEST] speed ramp 0->255->0 over 10s (descend direction)");
}

void rampTestTick() {
  if (!rampTestActive) return;

  unsigned long elapsed = millis() - rampTestStartMs;
  if (elapsed >= RAMP_TEST_DURATION_MS) {
    motorStop();
    rampTestActive = false;
    Serial.println("[TEST] ramp complete");
    return;
  }

  // Triangle ramp
  int speed;
  if (elapsed < RAMP_TEST_DURATION_MS / 2) {
    speed = (int)((elapsed * 255) / (RAMP_TEST_DURATION_MS / 2));
  } else {
    speed = (int)(((RAMP_TEST_DURATION_MS - elapsed) * 255) / (RAMP_TEST_DURATION_MS / 2));
  }
  // Drive ENB directly so logs aren't spammed by motorSetSpeed's direction-change branch.
  digitalWrite(MOTOR_IN3, HIGH);
  digitalWrite(MOTOR_IN4, LOW);
  analogWrite(MOTOR_ENB, speed);
  motorDir = MOTOR_DESCEND;
  motorSpeed = speed;

  if (millis() - rampTestLastLogMs >= 500) {
    Serial.printf("[TEST] t=%lu ms  speed=%d/255\n", elapsed, speed);
    rampTestLastLogMs = millis();
  }
}

// In-water HOLD-speed calibration. Runs a PWM sweep at the deep band, picks the PWM
// with the smallest absolute drift, and persists it to /cali.txt.
enum CaliPhase { CALI_IDLE, CALI_DESCEND, CALI_SWEEP };
CaliPhase caliPhase = CALI_IDLE;
unsigned long caliPhaseStartMs = 0;
unsigned long caliStepStartMs = 0;
int caliStepIndex = 0;
float caliStepStartDepth = 0.0f;
float caliBestAbsDrift = 1e9f;
int caliBestPwm = MOTOR_SPEED_HOLD_DEFAULT;

const int CALI_PWMS[] = {60, 80, 100, 120, 140, 160, 180};
const int CALI_NUM_PWMS = sizeof(CALI_PWMS) / sizeof(CALI_PWMS[0]);
const unsigned long CALI_STEP_MS = 4000UL;
const unsigned long CALI_DESCEND_TIMEOUT_MS = 60000UL;

void caliStart() {
  caliPhase = CALI_DESCEND;
  caliPhaseStartMs = millis();
  caliStepIndex = 0;
  caliBestAbsDrift = 1e9f;
  caliBestPwm = MOTOR_SPEED_HOLD_DEFAULT;
  motorSet(MOTOR_DESCEND);   // full speed to reach deep band
  Serial.printf("[CALI] start — descending to >= %.2f m\n", DEEP_MIN_M);
}

void caliAbort(const char *reason) {
  Serial.printf("[CALI] abort: %s\n", reason);
  motorStop();
  caliPhase = CALI_IDLE;
}

void caliTick(float depth) {
  if (caliPhase == CALI_IDLE) return;

  // Emergency depth guard (mirrors missionTick's MAX_DEPTH_M)
  if (depth > MAX_DEPTH_M) {
    caliAbort("depth exceeded MAX_DEPTH_M");
    return;
  }

  switch (caliPhase) {
    case CALI_DESCEND:
      if (depth >= DEEP_MIN_M) {
        Serial.println("[CALI] descend complete — beginning PWM sweep");
        caliPhase = CALI_SWEEP;
        caliStepIndex = 0;
        caliStepStartMs = millis();
        caliStepStartDepth = depth;
        motorSetSpeed(MOTOR_DESCEND, CALI_PWMS[0]);
        Serial.printf("[CALI] step 1/%d pwm=%d  start_depth=%.2f m\n",
                      CALI_NUM_PWMS, CALI_PWMS[0], depth);
      } else if (millis() - caliPhaseStartMs > CALI_DESCEND_TIMEOUT_MS) {
        caliAbort("descend timeout");
      }
      break;

    case CALI_SWEEP:
      if (millis() - caliStepStartMs >= CALI_STEP_MS) {
        float drift = depth - caliStepStartDepth;
        Serial.printf("[CALI] step %d/%d pwm=%d  drift=%+.3f m\n",
                      caliStepIndex + 1, CALI_NUM_PWMS, CALI_PWMS[caliStepIndex], drift);
        float absDrift = drift < 0 ? -drift : drift;
        if (absDrift < caliBestAbsDrift) {
          caliBestAbsDrift = absDrift;
          caliBestPwm = CALI_PWMS[caliStepIndex];
        }
        caliStepIndex++;
        if (caliStepIndex >= CALI_NUM_PWMS) {
          motorStop();
          Serial.printf("[CALI] best pwm=%d (|drift|=%.3f m)\n", caliBestPwm, caliBestAbsDrift);
          saveHoldSpeed(caliBestPwm);
          motorSpeedHold = caliBestPwm;
          char resp[40];
          snprintf(resp, sizeof(resp), "CALI_OK pwm=%d", caliBestPwm);
          if (espNowReady) esp_now_send(stationMac, (const uint8_t *)resp, strlen(resp));
          caliPhase = CALI_IDLE;
        } else {
          caliStepStartMs = millis();
          caliStepStartDepth = depth;
          motorSetSpeed(MOTOR_DESCEND, CALI_PWMS[caliStepIndex]);
        }
      }
      break;

    default: break;
  }
}

// One-way buoyancy controller for HOLD states.
// Strategy: pump (sink) when shallower than the band midpoint, otherwise stop and let
// natural buoyancy bring the float back up. Returns true while depth is inside the band.
bool holdControl(float depth, float lo, float hi) {
  float midpoint = (lo + hi) * 0.5f;
  if (depth < midpoint) {
    motorSetSpeed(MOTOR_DESCEND, motorSpeedHold);
  } else {
    motorStop();   // rise passively
  }
  return (depth >= lo && depth <= hi);
}

void missionTick(float depth) {
  if (missionState == MS_IDLE || missionState == MS_DONE) {
    motorStop();
    return;
  }

  // Emergency: too deep -> stop pump, let buoyancy bring the float up
  if (depth > MAX_DEPTH_M) {
    Serial.printf("[SAFETY] depth %.2f m > %.2f m — pump off, surfacing\n", depth, MAX_DEPTH_M);
    motorStop();
    enterState(MS_SURFACE);
    return;
  }

  // Watchdog only on the active-pumping state (DESCEND). Rise phases rely on natural buoyancy
  // and may legitimately take several minutes, so they have no timeout.
  if (missionState == MS_DESCEND && (millis() - stateStartMs) > STATE_TIMEOUT_MS) {
    Serial.printf("[SAFETY] DESCEND timeout — pump off, surfacing\n");
    motorStop();
    enterState(MS_SURFACE);
    return;
  }

  switch (missionState) {
    case MS_DESCEND:
      motorSet(MOTOR_DESCEND);
      if (depth >= DEEP_MIN_M) enterState(MS_HOLD_DEEP);
      break;

    case MS_HOLD_DEEP: {
      bool inBand = holdControl(depth, DEEP_MIN_M, DEEP_MAX_M);
      if (inBand) {
        if (holdStartMs == 0) holdStartMs = millis();
        if (millis() - holdStartMs >= HOLD_DURATION_MS) {
          enterState(MS_ASCEND_SHALLOW);
        }
      } else {
        if (holdStartMs != 0) {
          Serial.println("[HOLD_DEEP] band exit — restart 30s timer");
        }
        holdStartMs = 0;
      }
      break;
    }

    case MS_ASCEND_SHALLOW:
      motorStop();   // rise passively
      if (depth <= SHALLOW_MAX_M) enterState(MS_HOLD_SHALLOW);
      break;

    case MS_HOLD_SHALLOW: {
      bool inBand = holdControl(depth, SHALLOW_MIN_M, SHALLOW_MAX_M);
      if (inBand) {
        if (holdStartMs == 0) holdStartMs = millis();
        if (millis() - holdStartMs >= HOLD_DURATION_MS) {
          enterState(MS_SURFACE);
        }
      } else {
        if (holdStartMs != 0) {
          Serial.println("[HOLD_SHALLOW] band exit — restart 30s timer");
        }
        holdStartMs = 0;
      }
      break;
    }

    case MS_SURFACE:
      motorStop();   // rise passively
      if (depth <= SURFACE_M) {
        profileIndex++;
        if (profileIndex >= PROFILE_COUNT) {
          Serial.printf("[MISSION] all %d profiles complete\n", PROFILE_COUNT);
          if (espNowReady) esp_now_send(stationMac, (const uint8_t *)"MISSION_DONE", 12);
          enterState(MS_DONE);
        } else {
          Serial.printf("[MISSION] profile %d done — starting next\n", profileIndex);
          enterState(MS_DESCEND);
        }
      }
      break;

    default: break;
  }
}

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
#if USE_DEPTH_SENSOR
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
#else
  depthOffset = 0.0f;
  Serial.println("[zero calibration SKIPPED — USE_DEPTH_SENSOR=0]");
#endif
}

// Calibrated depth at the SENSOR position (surface = 0, positive when submerged).
float calibratedDepth() {
#if USE_DEPTH_SENSOR
  return sensor.depth() - depthOffset;
#else
  return 0.0f;
#endif
}

// Fake depth override — bench-test the mission state machine without water.
// When enabled, reportedDepth() returns the manually-set value instead of the sensor.
bool fakeDepthEnabled = false;
float fakeDepth = 0.0f;

// Float-bottom depth (mission reference for all bands and SURFACE detection).
// All packet values, mission state machine inputs, and graphs use this.
float reportedDepth() {
  if (fakeDepthEnabled) return fakeDepth;
  return calibratedDepth() + SENSOR_OFFSET_FROM_BOTTOM;
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
#if USE_DEPTH_SENSOR
  float kPa = sensor.pressure() * 0.1f;  // mbar -> kPa
#else
  float kPa = 0.0f;
#endif
  // Reported depth is the float BOTTOM (sensor reading + 6 in offset).
  float depthM = reportedDepth();
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
  else if (strcmp(cmd, "ABRT") == 0) cmdAbortRequested = true;
  else if (strcmp(cmd, "TEST") == 0) cmdTestRequested  = true;
  else if (strcmp(cmd, "FAKE") == 0) cmdFakeToggleRequested = true;
  else if (strcmp(cmd, "FK0M") == 0) cmdFakeSetValue = 0.00f;   // surface
  else if (strcmp(cmd, "FK2M") == 0) cmdFakeSetValue = 2.50f;   // deep target
  else if (strcmp(cmd, "FK4M") == 0) cmdFakeSetValue = 0.70f;   // shallow target (bottom)
  else if (strcmp(cmd, "FKUP") == 0) cmdFakeDelta    = +0.10f;
  else if (strcmp(cmd, "FKDN") == 0) cmdFakeDelta    = -0.10f;
  else if (strcmp(cmd, "CALI") == 0) cmdCaliRequested = true;
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

// Read calibrated HOLD PWM (1..255) from /cali.txt. Falls back silently if missing/invalid.
bool loadHoldSpeed() {
  if (!fsReady) return false;
  if (!LittleFS.exists(CALI_FILE)) return false;
  File f = LittleFS.open(CALI_FILE, "r");
  if (!f) return false;
  int v = f.parseInt();
  f.close();
  if (v <= 0 || v > 255) {
    Serial.printf("[CALI] %s contains out-of-range value %d — ignored\n", CALI_FILE, v);
    return false;
  }
  motorSpeedHold = v;
  Serial.printf("[CALI] loaded MOTOR_SPEED_HOLD = %d from %s\n", v, CALI_FILE);
  return true;
}

void saveHoldSpeed(int v) {
  if (!fsReady) return;
  File f = LittleFS.open(CALI_FILE, "w");
  if (!f) {
    Serial.println("[CALI] open(write) failed");
    return;
  }
  f.printf("%d\n", v);
  f.close();
  Serial.printf("[CALI] saved MOTOR_SPEED_HOLD = %d to %s\n", v, CALI_FILE);
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
  pinMode(MOTOR_IN3, OUTPUT);
  pinMode(MOTOR_IN4, OUTPUT);
  pinMode(MOTOR_ENB, OUTPUT);
  motorStop();  // ensure motor is off at boot

#if USE_DEPTH_SENSOR
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
#else
  Serial.println("[MS5837 SKIPPED — USE_DEPTH_SENSOR=0]");
  Serial.println();
#endif

  calibrateZero();
  setupLittleFS();
  loadHoldSpeed();
  setupEspNow();

  Serial.println("Press BOOT button to recalibrate (do this after placing on the surface).");
  Serial.println("--------------------------------");
  Serial.println(" Float serial keys (same as station wireless):");
  Serial.println("   S = START  (run 3-profile mission)");
  Serial.println("   X = ABORT  (stop motor, return to idle)");
  Serial.println("   T = TEST   (10s speed ramp self-test)");
  Serial.println("   C = CALI   (in-water HOLD-PWM auto-calibration → /cali.txt)");
  Serial.println("   D = DUMP   (read LittleFS mission log to serial)");
  Serial.println(" Float bench-test keys (no station equivalent):");
  Serial.println("   F = toggle fake-depth mode (uses fakeDepth instead of sensor)");
  Serial.println("   + = fake depth +0.10 m");
  Serial.println("   - = fake depth -0.10 m");
  Serial.println("   0 = fake depth = 0.00 m  (surface preset)");
  Serial.println("   2 = fake depth = 2.50 m  (deep band preset)");
  Serial.println("   4 = fake depth = 0.70 m  (shallow band preset, bottom-ref)");
  Serial.println("--------------------------------");
  Serial.printf("Company number: %s, packet interval: %d ms\n", COMPANY_NUMBER, PACKET_INTERVAL_MS);
  Serial.printf("Mission: %d profiles. Bottom-referenced bands:\n", PROFILE_COUNT);
  Serial.printf("  DEEP    %.2f-%.2f m  (target 2.50 m, bottom of float)\n",
                DEEP_MIN_M, DEEP_MAX_M);
  Serial.printf("  SHALLOW %.2f-%.2f m  (target 0.40 m at top -> %.2f m at bottom)\n",
                SHALLOW_MIN_M, SHALLOW_MAX_M, 0.40f + FLOAT_HEIGHT_M);
  Serial.printf("Sensor offset from float bottom: %.4f m (sensor at midpoint, 6 in)\n",
                SENSOR_OFFSET_FROM_BOTTOM);
  Serial.printf("Surface check (bottom-referenced): reportedDepth <= %.2f m\n", SURFACE_M);
  Serial.printf("HOLD speed: %d/255 %s\n", motorSpeedHold,
                motorSpeedHold == MOTOR_SPEED_HOLD_DEFAULT
                  ? "(default — run 'C' in water to calibrate)"
                  : "(loaded from /cali.txt)");
  Serial.printf("Hold duration: %lu s\n", HOLD_DURATION_MS / 1000);
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

  // Serial command handling — keys mirror the station's wireless command keys.
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'S' || c == 's') {
      Serial.printf("[SERIAL] S — start mission (%d profiles)\n", PROFILE_COUNT);
      missionStart();
    }
    else if (c == 'T' || c == 't') {
      Serial.println("[SERIAL] T — speed ramp test");
      rampTestStart();
    }
    else if (c == 'X' || c == 'x') {
      Serial.println("[SERIAL] X — abort, motor off");
      rampTestActive = false;
      caliPhase = CALI_IDLE;
      motorStop();
      enterState(MS_IDLE);
    }
    else if (c == 'C' || c == 'c') {
      Serial.println("[SERIAL] C — auto-calibrate hold PWM");
      caliStart();
    }
    else if (c == 'D' || c == 'd') dumpLog();
    // Fake depth controls (bench testing without water)
    else if (c == 'F' || c == 'f') {
      fakeDepthEnabled = !fakeDepthEnabled;
      Serial.printf("[FAKE] %s — current value %.2f m\n",
                    fakeDepthEnabled ? "ON  (using fake depth)" : "OFF (using real sensor)",
                    fakeDepth);
    }
    else if (c == '+' || c == '=') {
      fakeDepth += 0.10f;
      Serial.printf("[FAKE] depth = %.2f m %s\n", fakeDepth,
                    fakeDepthEnabled ? "" : "(fake mode OFF — press F to apply)");
    }
    else if (c == '-' || c == '_') {
      fakeDepth -= 0.10f;
      if (fakeDepth < 0) fakeDepth = 0;
      Serial.printf("[FAKE] depth = %.2f m %s\n", fakeDepth,
                    fakeDepthEnabled ? "" : "(fake mode OFF — press F to apply)");
    }
    else if (c == '2') {
      fakeDepth = 2.50f;
      Serial.printf("[FAKE] depth = %.2f m (deep target preset)\n", fakeDepth);
    }
    else if (c == '4') {
      fakeDepth = 0.70f;   // bottom-referenced, ~middle of shallow band
      Serial.printf("[FAKE] depth = %.2f m (shallow target preset)\n", fakeDepth);
    }
    else if (c == '0') {
      fakeDepth = 0.0f;
      Serial.printf("[FAKE] depth = %.2f m (surface preset)\n", fakeDepth);
    }
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
    Serial.printf("[CMD] START — mission (%d profiles)\n", PROFILE_COUNT);
    missionStart();
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)"MISSION_START", 13);
  }
  if (cmdAbortRequested) {
    cmdAbortRequested = false;
    Serial.println("[CMD] ABORT — stop everything");
    rampTestActive = false;
    caliPhase = CALI_IDLE;
    motorStop();
    enterState(MS_IDLE);
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)"ABORTED", 7);
  }
  if (cmdCaliRequested) {
    cmdCaliRequested = false;
    Serial.println("[CMD] CALI — auto-calibrate hold PWM");
    caliStart();
  }
  if (cmdTestRequested) {
    cmdTestRequested = false;
    Serial.println("[CMD] TEST — speed ramp");
    rampTestStart();
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)"TEST_START", 10);
  }
  if (cmdFakeToggleRequested) {
    cmdFakeToggleRequested = false;
    fakeDepthEnabled = !fakeDepthEnabled;
    Serial.printf("[CMD] FAKE %s — depth %.2f m\n",
                  fakeDepthEnabled ? "ON" : "OFF", fakeDepth);
    char resp[40];
    snprintf(resp, sizeof(resp), "FAKE_%s_%.2fm",
             fakeDepthEnabled ? "ON" : "OFF", fakeDepth);
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)resp, strlen(resp));
  }
  if (cmdFakeSetValue >= 0.0f) {
    fakeDepth = cmdFakeSetValue;
    cmdFakeSetValue = -1.0f;
    Serial.printf("[CMD] fake depth = %.2f m\n", fakeDepth);
    char resp[32];
    snprintf(resp, sizeof(resp), "FK_SET_%.2fm", fakeDepth);
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)resp, strlen(resp));
  }
  if (cmdFakeDelta != 0.0f) {
    fakeDepth += cmdFakeDelta;
    if (fakeDepth < 0.0f) fakeDepth = 0.0f;
    cmdFakeDelta = 0.0f;
    Serial.printf("[CMD] fake depth = %.2f m\n", fakeDepth);
    char resp[32];
    snprintf(resp, sizeof(resp), "FK_NUDGE_%.2fm", fakeDepth);
    if (espNowReady) esp_now_send(stationMac, (const uint8_t *)resp, strlen(resp));
  }

#if USE_DEPTH_SENSOR
  sensor.read();
#endif

  // Dispatch order: ramp test → calibration → mission. Higher-priority modes preempt
  // the mission state machine while they run.
  rampTestTick();
  if (!rampTestActive) {
    caliTick(reportedDepth());
    if (caliPhase == CALI_IDLE) {
      missionTick(reportedDepth());
    }
  }

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
