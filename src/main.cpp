// office-co2-monitor
// Phase 2: display bring-up. Lights the round GC9A01 (240x240) over the
// EYESPI breakout and draws a test pattern that exercises color channels,
// full-screen extent, and refresh — so the soldered display path can be
// verified before the sensors are added.
//
// Still enables the I2C power rail and scans the bus (Phase 1) so the scan
// reports the SCD-41 (0x62) + DS3231 (0x68) once the QT chain is attached.
// With no QT connected they correctly report MISSING — that is expected.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

#include "config.h"
#include "version.h"

static Adafruit_GC9A01A tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

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
  Serial.printf("  DS3231 (0x68): %s\n", rtc ? "OK" : "MISSING");
  Serial.println(F("  (MISSING is expected until the QT chain is attached)\n"));
}

static void drawTestScreen() {
  tft.fillScreen(GC9A01A_BLACK);

  // Bezel ring — confirms full extent / centering on the round panel.
  tft.drawCircle(120, 120, 118, GC9A01A_WHITE);

  // Color-channel check — if any dot is wrong/missing, suspect that data line.
  tft.fillCircle(120, 58, 16, GC9A01A_RED);
  tft.fillCircle(92, 104, 16, GC9A01A_GREEN);
  tft.fillCircle(148, 104, 16, GC9A01A_BLUE);

  tft.setTextColor(GC9A01A_WHITE);
  tft.setTextSize(2);
  tft.setCursor(36, 140);   // ~centered for 14 chars * 12px
  tft.print("office-co2");

  tft.setTextSize(2);
  tft.setTextColor(GC9A01A_CYAN);
  tft.setCursor(84, 166);   // ~centered for "v0.2.0"
  tft.print("v" FIRMWARE_VERSION);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\noffice-co2-monitor  v%s\n", FIRMWARE_VERSION);
  Serial.println(F("Phase 2: display bring-up\n"));

  // Backlight full on (PWM dimming comes later).
  pinMode(TFT_LITE_PIN, OUTPUT);
  digitalWrite(TFT_LITE_PIN, HIGH);

  tft.begin();
  tft.setRotation(0);
  drawTestScreen();
  Serial.println(F("display: test pattern drawn"));

  enableI2CPower();
  Wire.begin();
  scanBus();
}

void loop() {
  // Live counter near the bottom proves the panel is refreshing, not frozen.
  static uint32_t last = 0;
  uint32_t secs = millis() / 1000;
  if (secs != last) {
    last = secs;
    tft.fillRect(70, 196, 100, 20, GC9A01A_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(GC9A01A_WHITE);
    tft.setCursor(78, 198);
    tft.printf("%lus", (unsigned long)secs);
  }
}
