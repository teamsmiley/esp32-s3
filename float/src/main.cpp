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

// Float geometry. Total length 13 in = 12 in tube + 0.5 in top cap + 0.5 in bottom cap.
// The depth sensor is mounted in the bottom cap and sits exactly at the float's bottom
// face — no mounting offset. All mission depth values below are expressed as the float
// BOTTOM's depth, so reportedDepth() == sensor reading directly.
// Bottom-mounting keeps the sensor submerged the longest as the float rises (never
// saturates at ~0 m the way a top- or mid-mounted sensor would).
#define FLOAT_HEIGHT_M            0.3302f   // 13 in (12" tube + 2 × 0.5" caps)
#define SENSOR_OFFSET_FROM_BOTTOM 0.0000f   // sensor sits flush with the float's bottom

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
// Atmospheric pressure variation produces ~±20 cm of noise on raw sensor.depth(),
// which the ±33 cm mission tolerance comfortably absorbs. Zero-point calibration
// removed: the sensor reading is used as bottom depth directly.

#if USE_DEPTH_SENSOR
MS5837 sensor;
#endif
unsigned long missionStartMs = 0;
unsigned long nextPacketMs = 0;

// Paired station board MAC. Unicast TX so other teams never receive our packets.
uint8_t stationMac[6] = {0xAC, 0xA7, 0x04, 0x13, 0x3A, 0xE8};
bool espNowReady = false;
bool fsReady = false;

// Wireless command flags (RX callback is ISR-like; heavy work runs in loop())
volatile bool  cmdDumpRequested       = false;
volatile bool  cmdPingRequested       = false;
volatile bool  cmdStartRequested      = false;
volatile bool  cmdAbortRequested      = false;
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

// In-water HOLD-speed calibration. Two trigger paths:
//   1. Manual `C` / `CALI` command — descends from surface, sweeps, saves, surfaces.
//   2. Automatic during the first mission HOLD_DEEP if /cali.txt is missing — sweep runs
//      inline before the 30-second hold timer starts. No user action required.
enum CaliPhase { CALI_IDLE, CALI_DESCEND, CALI_SWEEP };
CaliPhase caliPhase = CALI_IDLE;
unsigned long caliPhaseStartMs = 0;
unsigned long caliStepStartMs = 0;
int caliStepIndex = 0;
float caliStepStartDepth = 0.0f;
float caliBestAbsDrift = 1e9f;
int caliBestPwm = MOTOR_SPEED_HOLD_DEFAULT;
bool caliPending = true;     // cleared once calibration completes (manual or inline)
bool caliInline = false;     // true when calibration runs inside HOLD_DEEP, not standalone

const int CALI_PWMS[] = {60, 80, 100, 120, 140, 160, 180};
const int CALI_NUM_PWMS = sizeof(CALI_PWMS) / sizeof(CALI_PWMS[0]);
// Inline calibration uses a shorter sweep so the mission timer impact is bounded.
const int CALI_INLINE_PWMS[] = {70, 100, 130, 160};
const int CALI_INLINE_NUM_PWMS = sizeof(CALI_INLINE_PWMS) / sizeof(CALI_INLINE_PWMS[0]);
const unsigned long CALI_STEP_MS = 4000UL;
const unsigned long CALI_INLINE_STEP_MS = 3000UL;
const unsigned long CALI_DESCEND_TIMEOUT_MS = 60000UL;

// Picked PWM list and step duration based on inline vs standalone mode.
const int *caliCurrentPwms() {
  return caliInline ? CALI_INLINE_PWMS : CALI_PWMS;
}
int caliCurrentNumPwms() {
  return caliInline ? CALI_INLINE_NUM_PWMS : CALI_NUM_PWMS;
}
unsigned long caliCurrentStepMs() {
  return caliInline ? CALI_INLINE_STEP_MS : CALI_STEP_MS;
}

void caliStart() {
  caliInline = false;
  caliPhase = CALI_DESCEND;
  caliPhaseStartMs = millis();
  caliStepIndex = 0;
  caliBestAbsDrift = 1e9f;
  caliBestPwm = MOTOR_SPEED_HOLD_DEFAULT;
  motorSet(MOTOR_DESCEND);   // full speed to reach deep band
  Serial.printf("[CALI] start — descending to >= %.2f m\n", DEEP_MIN_M);
}

// Begin sweep immediately, assuming we are already in the deep band.
// Used for inline calibration that piggybacks on the first mission HOLD_DEEP.
void caliStartInlineAtDepth(float depth) {
  caliInline = true;
  caliPhase = CALI_SWEEP;
  caliPhaseStartMs = millis();
  caliStepIndex = 0;
  caliBestAbsDrift = 1e9f;
  caliBestPwm = MOTOR_SPEED_HOLD_DEFAULT;
  caliStepStartMs = millis();
  caliStepStartDepth = depth;
  motorSetSpeed(MOTOR_DESCEND, caliCurrentPwms()[0]);
  Serial.printf("[CALI inline] start at %.2f m — %d PWM steps × %lu ms\n",
                depth, caliCurrentNumPwms(), caliCurrentStepMs());
  Serial.printf("[CALI inline] step 1/%d pwm=%d\n",
                caliCurrentNumPwms(), caliCurrentPwms()[0]);
}

void caliAbort(const char *reason) {
  Serial.printf("[CALI] abort: %s\n", reason);
  motorStop();
  caliPhase = CALI_IDLE;
  caliInline = false;
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
        motorSetSpeed(MOTOR_DESCEND, caliCurrentPwms()[0]);
        Serial.printf("[CALI] step 1/%d pwm=%d  start_depth=%.2f m\n",
                      caliCurrentNumPwms(), caliCurrentPwms()[0], depth);
      } else if (millis() - caliPhaseStartMs > CALI_DESCEND_TIMEOUT_MS) {
        caliAbort("descend timeout");
      }
      break;

    case CALI_SWEEP: {
      const int *pwms = caliCurrentPwms();
      const int numPwms = caliCurrentNumPwms();
      const unsigned long stepMs = caliCurrentStepMs();
      if (millis() - caliStepStartMs >= stepMs) {
        float drift = depth - caliStepStartDepth;
        const char *tag = caliInline ? "CALI inline" : "CALI";
        Serial.printf("[%s] step %d/%d pwm=%d  drift=%+.3f m\n",
                      tag, caliStepIndex + 1, numPwms, pwms[caliStepIndex], drift);
        float absDrift = drift < 0 ? -drift : drift;
        if (absDrift < caliBestAbsDrift) {
          caliBestAbsDrift = absDrift;
          caliBestPwm = pwms[caliStepIndex];
        }
        caliStepIndex++;
        if (caliStepIndex >= numPwms) {
          motorStop();
          Serial.printf("[%s] best pwm=%d (|drift|=%.3f m)\n",
                        tag, caliBestPwm, caliBestAbsDrift);
          saveHoldSpeed(caliBestPwm);
          motorSpeedHold = caliBestPwm;
          caliPending = false;
          char resp[40];
          snprintf(resp, sizeof(resp), "CALI_OK pwm=%d", caliBestPwm);
          if (espNowReady) esp_now_send(stationMac, (const uint8_t *)resp, strlen(resp));
          caliPhase = CALI_IDLE;
          caliInline = false;
        } else {
          caliStepStartMs = millis();
          caliStepStartDepth = depth;
          motorSetSpeed(MOTOR_DESCEND, pwms[caliStepIndex]);
        }
      }
      break;
    }

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
      if (depth >= DEEP_MIN_M) {
        enterState(MS_HOLD_DEEP);
        // First time hitting the deep band without saved calibration: piggyback an inline
        // PWM sweep before the 30-second hold timer starts. This lets the float self-tune
        // mid-mission when there was no chance to run `C` in advance.
        if (caliPending) {
          Serial.println("[MISSION] no calibration on file — running inline sweep first");
          caliStartInlineAtDepth(depth);
        }
      }
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

// Raw depth at the sensor position (bottom of float, mounted flush).
// No zero calibration — the ±33 cm mission tolerance absorbs atmospheric noise.
float calibratedDepth() {
#if USE_DEPTH_SENSOR
  return sensor.depth();
#else
  return 0.0f;
#endif
}

// Float-bottom depth (mission reference for all bands and SURFACE detection).
// All packet values, mission state machine inputs, and graphs use this.
float reportedDepth() {
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
  else if (strcmp(cmd, "PING") == 0) cmdPingRequested = true;
  else if (strcmp(cmd, "STAR") == 0) cmdStartRequested = true;
  else if (strcmp(cmd, "ABRT") == 0) cmdAbortRequested = true;
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
  caliPending = false;
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
  Serial.println("=== MS5837 depth sensor (no zero calibration — raw reading used) ===");
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

  setupLittleFS();
  loadHoldSpeed();
  setupEspNow();

  Serial.println("Press BOOT button to reset the mission clock (no zero calibration is performed).");
  Serial.println("--------------------------------");
  Serial.println(" Float serial keys (same as station wireless):");
  Serial.println("   S = START  (run 3-profile mission)");
  Serial.println("   X = ABORT  (stop motor, return to idle)");
  Serial.println("   C = CALI   (in-water HOLD-PWM auto-calibration → /cali.txt)");
  Serial.println("   D = DUMP   (read LittleFS mission log to serial)");
  Serial.println("--------------------------------");
  Serial.printf("Company number: %s, packet interval: %d ms\n", COMPANY_NUMBER, PACKET_INTERVAL_MS);
  Serial.printf("Mission: %d profiles. Bottom-referenced bands:\n", PROFILE_COUNT);
  Serial.printf("  DEEP    %.2f-%.2f m  (target 2.50 m, bottom of float)\n",
                DEEP_MIN_M, DEEP_MAX_M);
  Serial.printf("  SHALLOW %.2f-%.2f m  (target 0.40 m at top -> %.2f m at bottom)\n",
                SHALLOW_MIN_M, SHALLOW_MAX_M, 0.40f + FLOAT_HEIGHT_M);
  Serial.printf("Sensor offset from float bottom: %.4f m (sensor flush with bottom)\n",
                SENSOR_OFFSET_FROM_BOTTOM);
  Serial.printf("Float height: %.4f m (13 in = 12\" tube + 2 × 0.5\" caps)\n", FLOAT_HEIGHT_M);
  Serial.printf("Surface check (bottom-referenced): reportedDepth <= %.2f m\n", SURFACE_M);
  Serial.printf("HOLD speed: %d/255 %s\n", motorSpeedHold,
                caliPending
                  ? "(default — auto-calibrates on first HOLD_DEEP, or run 'C' now)"
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
    Serial.println("\n[BOOT button — mission clock reset]");
    missionStartMs = millis();
    nextPacketMs = missionStartMs;
  }

  // Serial command handling — keys mirror the station's wireless command keys.
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'S' || c == 's') {
      Serial.printf("[SERIAL] S — start mission (%d profiles)\n", PROFILE_COUNT);
      missionStart();
    }
    else if (c == 'X' || c == 'x') {
      Serial.println("[SERIAL] X — abort, motor off");
      caliPhase = CALI_IDLE;
      motorStop();
      enterState(MS_IDLE);
    }
    else if (c == 'C' || c == 'c') {
      Serial.println("[SERIAL] C — auto-calibrate hold PWM");
      caliStart();
    }
    else if (c == 'D' || c == 'd') dumpLog();
  }

  // Wireless command flags (set in callback, executed here)
  if (cmdDumpRequested) {
    cmdDumpRequested = false;
    dumpLog();
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

#if USE_DEPTH_SENSOR
  sensor.read();
#endif

  // Calibration preempts the mission state machine while it runs.
  caliTick(reportedDepth());
  if (caliPhase == CALI_IDLE) {
    missionTick(reportedDepth());
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
