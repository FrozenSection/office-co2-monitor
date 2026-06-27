#include "settings.h"
#include "config.h"
#include <Preferences.h>

namespace settings {
Settings cfg;
}

static Preferences prefs;
static const char* NS = "co2cfg";
static const uint16_t SCHEMA_VERSION = 2;

static void loadDefaults(Settings& c) {
  c.frcReferencePpm = FRC_REFERENCE_PPM;
  c.lastFrcEpoch    = 0;
  c.profile         = PROFILE_SEALED;

  c.rotation   = DEFAULT_ROTATION;
  c.brightness = DEFAULT_BRIGHTNESS;
  c.tempUnitF  = DEFAULT_TEMP_UNIT_F;

  c.autoBrightness = DEFAULT_AUTO_BRIGHTNESS;
  c.brightnessMin  = DEFAULT_BRIGHT_MIN;
  c.brightnessMax  = DEFAULT_BRIGHT_MAX;
  c.luxLow         = DEFAULT_LUX_LOW;
  c.luxHigh        = DEFAULT_LUX_HIGH;

  c.aqGood = AQ_GOOD_PPM;
  c.aqFair = AQ_FAIR_PPM;
  c.aqPoor = AQ_POOR_PPM;

  c.calAgingDays   = CAL_AGING_DAYS;
  c.calStaleDays   = CAL_STALE_DAYS;
  c.calOverdueDays = CAL_OVERDUE_DAYS;

  strncpy(c.timezone, DEFAULT_TZ, sizeof(c.timezone) - 1);
  c.timezone[sizeof(c.timezone) - 1] = '\0';
  c.ntpEnabled = true;

  c.wifiSsid[0] = '\0';
  c.wifiPass[0] = '\0';
  strncpy(c.hostname, DEFAULT_HOSTNAME, sizeof(c.hostname) - 1);
  c.hostname[sizeof(c.hostname) - 1] = '\0';
}

void settings::begin() {
  loadDefaults(cfg);
  prefs.begin(NS, false);

  uint16_t ver = prefs.getUShort("ver", 0);
  size_t   len = prefs.getBytesLength("blob");
  if (ver == SCHEMA_VERSION && len == sizeof(Settings)) {
    prefs.getBytes("blob", &cfg, sizeof(Settings));
    Serial.println(F("settings: loaded from NVS"));
  } else {
    prefs.putUShort("ver", SCHEMA_VERSION);
    prefs.putBytes("blob", &cfg, sizeof(Settings));
    Serial.println(F("settings: seeded defaults"));
  }
  prefs.end();
}

void settings::save() {
  prefs.begin(NS, false);
  prefs.putUShort("ver", SCHEMA_VERSION);
  prefs.putBytes("blob", &cfg, sizeof(Settings));
  prefs.end();
}

bool settings::ascEnabled() {
  return cfg.profile == PROFILE_VENTILATED;
}

void settings::markRecalibrated(uint32_t epoch) {
  cfg.lastFrcEpoch = epoch;
  save();
}
