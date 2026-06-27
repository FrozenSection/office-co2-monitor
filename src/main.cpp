// office-co2-monitor
// Phase 1: I2C bring-up. Prove the STEMMA QT chain before touching the
// display or sensor drivers. Drives the Feather V2 I2C power rail, scans
// the bus, and confirms the SCD-41 (0x62) and DS3231 (0x68) are present.

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "version.h"

static void enableI2CPower() {
  // Without this the STEMMA QT port is unpowered and the scan finds nothing.
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  delay(50);  // let the rail settle
}

static void scanBus(bool &foundScd, bool &foundRtc) {
  foundScd = false;
  foundRtc = false;
  int count = 0;

  Serial.println(F("I2C scan:"));
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X", addr);
      if (addr == I2C_ADDR_SCD41)      { Serial.print(F("  <- SCD-41"));     foundScd = true; }
      if (addr == I2C_ADDR_DS3231)     { Serial.print(F("  <- DS3231"));     foundRtc = true; }
      if (addr == I2C_ADDR_RTC_EEPROM)   Serial.print(F("  <- RTC EEPROM"));
      Serial.println();
      count++;
    }
  }
  if (count == 0) Serial.println(F("  (nothing found)"));
}

static void report(bool scd, bool rtc) {
  Serial.println();
  Serial.printf("  SCD-41 (0x62): %s\n", scd ? "OK" : "MISSING");
  Serial.printf("  DS3231 (0x68): %s\n", rtc ? "OK" : "MISSING");
  Serial.println(scd && rtc ? F("  => chain OK")
                            : F("  => CHECK QT CABLES / WIRING"));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\noffice-co2-monitor  v%s\n", FIRMWARE_VERSION);
  Serial.println(F("Phase 1: I2C bring-up\n"));

  enableI2CPower();
  Wire.begin();

  bool scd, rtc;
  scanBus(scd, rtc);
  report(scd, rtc);
}

void loop() {
  // Re-scan periodically so cables can be hot-checked on the bench.
  delay(5000);
  bool scd, rtc;
  scanBus(scd, rtc);
  report(scd, rtc);
}
