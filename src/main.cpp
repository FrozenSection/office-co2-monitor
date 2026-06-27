// office-co2-monitor
// Phase 3: SCD-41 CO2 sensor. Reads CO2 / temperature / humidity over the
// STEMMA QT chain and shows a live, color-coded readout on the round display.
//
// Automatic self-calibration (ASC) is DISABLED here: this office never sees
// fresh outdoor air, so ASC would slowly miscalibrate. Accuracy instead comes
// from on-demand forced recalibration (the fresh-air walk) — wired up later.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <SensirionI2cScd4x.h>

#include "config.h"
#include "version.h"
#include "settings.h"

static Adafruit_GC9A01A tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static SensirionI2cScd4x scd4x;

// ---- helpers --------------------------------------------------------------

static void enableI2CPower() {
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(50);
}

static void scanBus() {
  bool scd = false, rtc = false;
  Serial.println(F("I2C scan:"));
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X\n", addr);
      if (addr == I2C_ADDR_SCD41)  scd = true;
      if (addr == I2C_ADDR_DS3231) rtc = true;
    }
  }
  Serial.printf("  SCD-41 (0x62): %s\n", scd ? "OK" : "MISSING");
  Serial.printf("  DS3231 (0x68): %s  (RTC is Phase 4)\n\n", rtc ? "OK" : "MISSING");
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
  if (co2 <= settings::cfg.aqPoor) { *label = "POOR"; return tft.color565(255, 140, 0); }
  *label = "BAD";
  return GC9A01A_RED;
}

static void drawCentered(int y, uint8_t size, uint16_t color, const char* s) {
  int w = (int)strlen(s) * 6 * size;  // GFX default font: 6px/char * size
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(120 - w / 2, y);
  tft.print(s);
}

static void drawStatic() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.drawCircle(120, 120, 118, GC9A01A_WHITE);   // bezel ring
  drawCentered(150, 2, tft.color565(130, 130, 130), "ppm");
  drawCentered(100, 2, GC9A01A_WHITE, "warming up");
}

static void showReading(uint16_t co2, float tempC, float hum) {
  const char* label;
  uint16_t color = co2Color(co2, &label);

  // status word
  tft.fillRect(30, 48, 180, 22, GC9A01A_BLACK);
  drawCentered(50, 2, color, label);

  // big CO2 number
  char num[8];
  snprintf(num, sizeof(num), "%u", co2);
  tft.fillRect(10, 96, 220, 48, GC9A01A_BLACK);
  drawCentered(100, 5, color, num);

  // temp + relative humidity (unit from settings)
  float tShow = settings::cfg.tempUnitF ? (tempC * 9.0f / 5.0f + 32.0f) : tempC;
  char  tUnit = settings::cfg.tempUnitF ? 'F' : 'C';
  int   rh    = (int)(hum + 0.5f);
  char  bot[20];
  snprintf(bot, sizeof(bot), "%d%c  %d%%", (int)(tShow + 0.5f), tUnit, rh);
  tft.fillRect(16, 172, 208, 20, GC9A01A_BLACK);
  drawCentered(176, 2, GC9A01A_WHITE, bot);
}

// ---- lifecycle ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\noffice-co2-monitor  v%s\n", FIRMWARE_VERSION);
  Serial.println(F("Phase 4: settings layer + display IA\n"));

  settings::begin();

  pinMode(TFT_LITE_PIN, OUTPUT);
  analogWrite(TFT_LITE_PIN, settings::cfg.brightness);   // backlight duty
  tft.begin();
  tft.setRotation(settings::cfg.rotation);
  drawStatic();

  enableI2CPower();
  Wire.begin();
  scanBus();

  scd4x.begin(Wire, I2C_ADDR_SCD41);

  // Ensure idle before configuring (it may still be measuring from last run).
  logScdError("stop", scd4x.stopPeriodicMeasurement());
  delay(500);

  uint64_t serial = 0;
  if (scd4x.getSerialNumber(serial) == 0) {
    Serial.printf("SCD-41 serial: 0x%llX\n", (unsigned long long)serial);
  } else {
    Serial.println(F("SCD-41 not responding — check the QT chain"));
  }

  // ASC follows the location profile (off for a sealed office).
  bool asc = settings::ascEnabled();
  logScdError("set ASC", scd4x.setAutomaticSelfCalibrationEnabled(asc ? 1 : 0));
  logScdError("start", scd4x.startPeriodicMeasurement());
  Serial.printf("ASC %s (profile=%u); periodic measurement started\n\n",
                asc ? "ENABLED" : "disabled", settings::cfg.profile);
}

void loop() {
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 1000) return;   // SCD-41 updates every ~5s
  lastPoll = millis();

  bool ready = false;
  if (scd4x.getDataReadyStatus(ready) != 0 || !ready) return;

  uint16_t co2 = 0;
  float tempC = 0, hum = 0;
  int16_t err = scd4x.readMeasurement(co2, tempC, hum);
  if (err) { logScdError("read", err); return; }
  if (co2 == 0) return;   // invalid/warming-up sample

  Serial.printf("CO2=%u ppm  T=%.1fC  RH=%.0f%%\n", co2, tempC, hum);
  showReading(co2, tempC, hum);
}
