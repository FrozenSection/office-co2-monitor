#pragma once
#include <Arduino.h>

// Persistent, user-editable settings, backed by NVS (ESP32 Preferences).
// Defaults come from config.h on first boot; the config web UI writes here.
// Stored as a versioned blob — a schema bump falls back to defaults rather
// than loading a stale layout (acceptable during development).

enum Profile : uint8_t {
  PROFILE_SEALED    = 0,   // office with no fresh air: ASC off, FRC-driven
  PROFILE_VENTILATED = 1,  // home/windows: ASC on, lighter reminders
};

struct Settings {
  // --- calibration ---
  uint16_t frcReferencePpm;   // fresh-air reference for forced recalibration
  uint32_t lastFrcEpoch;      // unix time of last FRC (0 = never)
  uint8_t  profile;           // Profile enum; drives ASC on/off

  // --- display ---
  uint8_t  rotation;          // 0..3
  uint8_t  brightness;        // 0..255 fixed duty (used when auto is off / no lux)
  bool     tempUnitF;         // true=degF, false=degC

  // --- auto-brightness (only applied when a VEML7700 is present) ---
  bool     autoBrightness;
  uint8_t  brightnessMin;     // night floor
  uint8_t  brightnessMax;     // bright-room ceiling
  uint16_t luxLow;            // lux mapped to brightnessMin
  uint16_t luxHigh;           // lux mapped to brightnessMax

  // --- air-quality tier ceilings (ppm) ---
  uint16_t aqGood;            // <= GOOD
  uint16_t aqFair;            // <= FAIR
  uint16_t aqPoor;            // <= POOR ; else BAD

  // --- calibration-confidence tiers (days since last FRC) ---
  uint16_t calAgingDays;
  uint16_t calStaleDays;
  uint16_t calOverdueDays;

  // --- time ---
  char     timezone[40];      // POSIX TZ string
  bool     ntpEnabled;

  // --- wifi (STA, optional) ---
  char     wifiSsid[33];
  char     wifiPass[64];
  char     hostname[24];
  bool     staEnabled;        // stay connected to home WiFi + serve LAN page
  char     webPassword[33];   // HTTP basic auth for settings + OTA (blank = off)
  uint16_t logIntervalSec;    // data-log sample period
  uint16_t altitudeM;         // SCD-41 altitude compensation (m)
  uint16_t tempOffsetC10;     // SCD-41 temperature offset (degC * 10)
  uint8_t  gammaX10;          // auto-brightness dimming gamma * 10
};
// NOTE: only ever APPEND fields (settings::begin migrates by partial load).

namespace settings {
  extern Settings cfg;

  void begin();                       // load from NVS, or seed defaults
  void save();                        // persist cfg
  bool ascEnabled();                  // derived: true for ventilated profile
  void markRecalibrated(uint32_t epoch);  // record an FRC + persist
}
