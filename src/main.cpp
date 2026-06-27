// office-co2-monitor
// Phase 6: main-screen renderer. Realizes the round-display information
// architecture — outer CO2-tier ring, big CO2 number + status word, time,
// temp/RH, and a progressively-disclosed calibration chip — using per-zone
// change detection so the panel updates without flicker. Optional sensors
// (VEML7700 lux, DS3231 time) are auto-detected; zones with no live data
// source stay in a graceful empty state.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_VEML7700.h>
#include <RTClib.h>

#include "config.h"
#include "version.h"
#include "settings.h"

static Adafruit_GC9A01A tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static SensirionI2cScd4x scd4x;
static Adafruit_VEML7700 veml;
static RTC_DS3231 rtc;

static bool     hasLux = false;     // VEML7700 detected
static bool     hasRtc = false;     // DS3231 detected
static float    gLux   = -1;        // smoothed lux (-1 until first read)
static uint16_t gCo2   = 0;         // last good CO2 (0 = none yet)
static float    gTempC = 0, gHum = 0;

enum CalState { CAL_UNKNOWN, CAL_FRESH, CAL_AGING, CAL_STALE, CAL_OVERDUE };

// ---- small helpers ---------------------------------------------------------

static inline uint16_t cGrey()   { return tft.color565(130, 130, 130); }
static inline uint16_t cOrange() { return tft.color565(255, 140, 0); }

static void enableI2CPower() {
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(50);
}

static void scanBus() {
  bool scd = false, rtcFound = false;
  Serial.println(F("I2C scan:"));
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X\n", addr);
      if (addr == I2C_ADDR_SCD41)  scd = true;
      if (addr == I2C_ADDR_DS3231) rtcFound = true;
    }
  }
  Serial.printf("  SCD-41 (0x62): %s\n", scd ? "OK" : "MISSING");
  Serial.printf("  DS3231 (0x68): %s\n\n", rtcFound ? "OK" : "MISSING");
}

static void logScdError(const char* ctx, int16_t err) {
  if (err == 0) return;
  char msg[64];
  errorToString((uint16_t)err, msg, sizeof(msg));
  Serial.printf("SCD-41 %s error: %s\n", ctx, msg);
}

// Air-quality tier -> color + short label. Logic only; palette is provisional
// (designer will refine, and the amber/amber collision is a known TODO).
static uint16_t co2Color(uint16_t co2, const char** label) {
  if (co2 <= settings::cfg.aqGood) { *label = "GOOD"; return GC9A01A_GREEN; }
  if (co2 <= settings::cfg.aqFair) { *label = "FAIR"; return GC9A01A_YELLOW; }
  if (co2 <= settings::cfg.aqPoor) { *label = "POOR"; return cOrange(); }
  *label = "BAD";
  return GC9A01A_RED;
}

static CalState calState(bool timeValid, uint32_t nowEpoch) {
  if (!timeValid || settings::cfg.lastFrcEpoch == 0) return CAL_UNKNOWN;
  uint32_t days = (nowEpoch - settings::cfg.lastFrcEpoch) / 86400UL;
  if (days >= settings::cfg.calOverdueDays) return CAL_OVERDUE;
  if (days >= settings::cfg.calStaleDays)   return CAL_STALE;
  if (days >= settings::cfg.calAgingDays)   return CAL_AGING;
  return CAL_FRESH;
}

// Clear a centered box and draw centered text in it. Boxes are sized per
// zone to their own max content and kept inside the ring (r112), so a redraw
// never erases the ring.
static void drawZone(int cy, int boxW, int boxH, uint8_t size,
                     uint16_t color, const char* s) {
  tft.fillRect(120 - boxW / 2, cy - boxH / 2, boxW, boxH, GC9A01A_BLACK);
  if (!s || !*s) return;
  int w = (int)strlen(s) * 6 * size;   // GFX default font: 6px/char * size
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(120 - w / 2, cy - (8 * size) / 2);
  tft.print(s);
}

static void drawRing(uint16_t color) {
  for (int r = 112; r <= 119; r++) tft.drawCircle(120, 120, r, color);
}

// ---- main screen -----------------------------------------------------------
//
// Zone centers (rotation 0, 240x240). Each box is sized to its own max
// content; all stay inside the ring so updates can't erase it.
//   ring   r112..119   CO2 tier color
//   time   cy36        HH:MM (or --:-- until set)
//   status cy70        GOOD/FAIR/POOR/BAD
//   co2    cy110 (sz5) big number
//   ppm    cy152       static label
//   tRH    cy176       temp + humidity
//   chip   cy198       calibration prompt (hidden unless aging+)

static void mainScreenEnter() {
  tft.fillScreen(GC9A01A_BLACK);
  drawZone(152, 48, 18, 2, cGrey(), "ppm");
  drawZone(110, 128, 46, 2, GC9A01A_WHITE, "warming up");
  drawZone(36, 72, 20, 2, cGrey(), "--:--");
}

static void renderMain(uint16_t co2, float tempC, float hum,
                       bool timeValid, int hh, int mm, CalState cal) {
  static uint16_t    lastCo2   = 0xFFFF;
  static uint16_t    lastRing  = 0;          // 0 = not yet drawn
  static const char* lastLabel = nullptr;
  static CalState    lastCal   = (CalState)255;
  static char        lastTime[8] = "";
  static char        lastBot[20] = "";

  const char* label;
  uint16_t tier     = co2Color(co2, &label);
  uint16_t numColor = (cal == CAL_OVERDUE) ? cGrey() : tier;  // grey = untrusted

  if (tier != lastRing) { drawRing(tier); lastRing = tier; }

  // status word + big number (also redraw if overdue-greying toggled)
  if (co2 != lastCo2 || label != lastLabel || cal != lastCal) {
    drawZone(70, 60, 22, 2, numColor, label);
    char num[8];
    snprintf(num, sizeof(num), "%u", co2);
    drawZone(110, 128, 46, 5, numColor, num);
    lastCo2 = co2;
    lastLabel = label;
  }

  // time
  char ts[8];
  if (timeValid) snprintf(ts, sizeof(ts), "%02d:%02d", hh, mm);
  else           strcpy(ts, "--:--");
  if (strcmp(ts, lastTime) != 0) {
    drawZone(36, 72, 20, 2, timeValid ? GC9A01A_WHITE : cGrey(), ts);
    strcpy(lastTime, ts);
  }

  // temp + humidity (unit from settings)
  float tShow = settings::cfg.tempUnitF ? (tempC * 9.0f / 5.0f + 32.0f) : tempC;
  char  tUnit = settings::cfg.tempUnitF ? 'F' : 'C';
  char  bot[20];
  snprintf(bot, sizeof(bot), "%d%c  %d%%",
           (int)(tShow + 0.5f), tUnit, (int)(hum + 0.5f));
  if (strcmp(bot, lastBot) != 0) {
    drawZone(176, 128, 20, 2, GC9A01A_WHITE, bot);
    strcpy(lastBot, bot);
  }

  // calibration chip — hidden when fresh/unknown, escalates with age
  if (cal != lastCal) {
    const char* msg = nullptr;
    uint16_t    c   = GC9A01A_YELLOW;
    switch (cal) {
      case CAL_AGING:   msg = "check cal";        c = GC9A01A_YELLOW; break;
      case CAL_STALE:   msg = "recalibrate soon"; c = cOrange();      break;
      case CAL_OVERDUE: msg = "recal overdue";    c = GC9A01A_RED;    break;
      default: break;   // FRESH / UNKNOWN -> stay hidden
    }
    drawZone(198, 104, 16, 1, c, msg ? msg : "");
    lastCal = cal;
  }
}

// Optional ambient-light auto-brightness. Reads the VEML7700 (if present),
// smooths it, maps lux -> backlight between the configured floor/ceiling, and
// slews gently so the panel never visibly steps.
static void updateLuxAndBrightness() {
  static uint32_t last = 0;
  static float    ema  = -1;
  static int      applied = -1;
  if (!hasLux) return;
  if (millis() - last < 400) return;
  last = millis();

  float lux = veml.readLux();
  if (lux >= 0) {
    ema = (ema < 0) ? lux : (ema * 0.7f + lux * 0.3f);
    gLux = ema;
  }
  if (!settings::cfg.autoBrightness) return;

  float lo = settings::cfg.luxLow, hi = settings::cfg.luxHigh;
  float t = (hi > lo) ? (gLux - lo) / (hi - lo) : 1.0f;
  if (t < 0) t = 0; else if (t > 1) t = 1;
  int target = settings::cfg.brightnessMin +
               (int)(t * (settings::cfg.brightnessMax - settings::cfg.brightnessMin));

  if (applied < 0) applied = target;
  if      (applied < target) applied += min(8, target - applied);
  else if (applied > target) applied -= min(8, applied - target);
  analogWrite(TFT_LITE_PIN, applied);
}

// ---- lifecycle ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\noffice-co2-monitor  v%s\n", FIRMWARE_VERSION);
  Serial.println(F("Phase 6: main-screen renderer\n"));

  settings::begin();

  pinMode(TFT_LITE_PIN, OUTPUT);
  analogWrite(TFT_LITE_PIN, settings::cfg.brightness);
  tft.begin();
  tft.setRotation(settings::cfg.rotation);
  mainScreenEnter();

  enableI2CPower();
  Wire.begin();
  scanBus();

  // Optional ambient-light sensor (auto-detected) -> enables auto-brightness.
  Wire.beginTransmission(I2C_ADDR_VEML7700);
  if (Wire.endTransmission() == 0 && veml.begin(&Wire)) {
    hasLux = true;
    veml.setGain(VEML7700_GAIN_1_8);
    veml.setIntegrationTime(VEML7700_IT_100MS);
    Serial.println(F("VEML7700: present -> auto-brightness available"));
  } else {
    Serial.println(F("VEML7700: not found -> fixed brightness"));
  }

  // RTC — present even without a coin cell; time is invalid until set.
  if (rtc.begin(&Wire)) {
    hasRtc = true;
    Serial.println(rtc.lostPower() ? F("DS3231: present, time NOT set")
                                   : F("DS3231: present, time set"));
  } else {
    Serial.println(F("DS3231: not found"));
  }

  scd4x.begin(Wire, I2C_ADDR_SCD41);
  logScdError("stop", scd4x.stopPeriodicMeasurement());
  delay(500);

  uint64_t serial = 0;
  if (scd4x.getSerialNumber(serial) == 0) {
    Serial.printf("SCD-41 serial: 0x%llX\n", (unsigned long long)serial);
  } else {
    Serial.println(F("SCD-41 not responding — check the QT chain"));
  }

  bool asc = settings::ascEnabled();
  logScdError("set ASC", scd4x.setAutomaticSelfCalibrationEnabled(asc ? 1 : 0));
  logScdError("start", scd4x.startPeriodicMeasurement());
  Serial.printf("ASC %s (profile=%u); measuring\n\n",
                asc ? "ENABLED" : "disabled", settings::cfg.profile);
}

void loop() {
  updateLuxAndBrightness();

  static uint32_t lastTick = 0;
  if (millis() - lastTick < 1000) { delay(5); return; }
  lastTick = millis();

  // Refresh CO2 when a new sample is ready (~every 5s).
  bool ready = false;
  if (scd4x.getDataReadyStatus(ready) == 0 && ready) {
    uint16_t co2 = 0;
    float tempC = 0, hum = 0;
    int16_t err = scd4x.readMeasurement(co2, tempC, hum);
    if (err) {
      logScdError("read", err);
    } else if (co2 != 0) {
      gCo2 = co2; gTempC = tempC; gHum = hum;
      if (hasLux)
        Serial.printf("CO2=%u ppm  T=%.1fC  RH=%.0f%%  lux=%.0f\n", co2, tempC, hum, gLux);
      else
        Serial.printf("CO2=%u ppm  T=%.1fC  RH=%.0f%%\n", co2, tempC, hum);
    }
  }

  // Refresh time + calibration state every tick (cheap; change-detected draw).
  bool timeValid = false;
  int  hh = 0, mm = 0;
  uint32_t nowEpoch = 0;
  if (hasRtc && !rtc.lostPower()) {
    DateTime n = rtc.now();
    hh = n.hour(); mm = n.minute(); nowEpoch = n.unixtime();
    timeValid = true;
  }

  if (gCo2 != 0)
    renderMain(gCo2, gTempC, gHum, timeValid, hh, mm, calState(timeValid, nowEpoch));
}
