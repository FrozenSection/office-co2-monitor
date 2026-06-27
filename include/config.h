#pragma once

// =====================================================================
// I2C — STEMMA QT chain (SCD-41 -> DS3231)
// =====================================================================
#define I2C_ADDR_SCD41      0x62
#define I2C_ADDR_DS3231     0x68
#define I2C_ADDR_RTC_EEPROM 0x57   // AT24C32 on the DS3231 module (harmless)

// The Feather V2 powers its STEMMA QT / I2C port through this pin.
// It MUST be driven HIGH before Wire.begin() or NO I2C device responds
// (classic "all my sensors are dead" red herring).
#ifndef NEOPIXEL_I2C_POWER
#define NEOPIXEL_I2C_POWER 2
#endif

// =====================================================================
// Forced recalibration (FRC) — the "fresh air walk"
// =====================================================================
#define FRC_REFERENCE_PPM   420   // outdoor fresh-air CO2 reference
#define FRC_EQUILIBRATE_SEC 180   // settle time outside before FRC fires
#define RECAL_BUTTON_PIN    27    // PROVISIONAL — momentary btn, INPUT_PULLUP

// =====================================================================
// Calibration-confidence tiers (days since last FRC).
// Logic only — palette is deliberately kept separate (designer TODO).
// =====================================================================
#define CAL_AGING_DAYS    7    // gentle hint
#define CAL_STALE_DAYS    14   // persistent prompt
#define CAL_OVERDUE_DAYS  21   // grey the reading, mark unverified

// =====================================================================
// Display — EYESPI / SPI (GC9A01 240x240 round)
// PROVISIONAL pins; finalize against the EYESPI breakout in display phase.
// =====================================================================
#define TFT_CS_PIN   33
#define TFT_DC_PIN   15
#define TFT_RST_PIN  32
#define TFT_LITE_PIN 14   // backlight, PWM-capable for dimming
