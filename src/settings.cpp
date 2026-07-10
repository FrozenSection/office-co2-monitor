#include "settings.h"
#include "config.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace settings {
Settings cfg;
}

static Preferences prefs;
static const char* NS = "co2cfg";
static const uint16_t SCHEMA_VERSION = 8;
// save() is called from the loop (FRC walk) AND the async_tcp task (web save);
// the shared Preferences session isn't re-entrant — a collision silently drops
// one save, or persists a blob missing the just-written lastFrcEpoch.
static SemaphoreHandle_t gMx = nullptr;

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
  c.staEnabled = DEFAULT_STA_ENABLED;
  c.webPassword[0] = '\0';
  c.logIntervalSec = DEFAULT_LOG_INTERVAL_SEC;
  c.altitudeM      = DEFAULT_ALTITUDE_M;
  c.tempOffsetC10  = DEFAULT_TEMP_OFFSET_C10;
  c.gammaX10       = DEFAULT_GAMMA_X10;
  c.brightnessBias = 0;
}

void settings::begin() {
  gMx = xSemaphoreCreateMutex();     // created before the web server exists
  loadDefaults(cfg);
  prefs.begin(NS, false);

  size_t len = prefs.getBytesLength("blob");
  if (len > 0 && len <= sizeof(Settings)) {
    // Partial load: stored bytes fill the matching prefix; appended (newer)
    // fields keep their defaults. Safe because we only ever append fields.
    prefs.getBytes("blob", &cfg, sizeof(Settings));
    Serial.printf("settings: loaded (%u of %u bytes)\n",
                  (unsigned)len, (unsigned)sizeof(Settings));
  } else {
    Serial.println(F("settings: seeded defaults"));
  }
  // Heal values that can't be valid. A newly-appended field can fall into the
  // struct's tail padding without growing sizeof(Settings) — then the partial
  // load above doesn't see it as "new" and reads back the old padding (e.g. 0).
  // Range-check such fields so they fall back to defaults instead of garbage.
  if (cfg.gammaX10 < 10 || cfg.gammaX10 > 30) cfg.gammaX10 = DEFAULT_GAMMA_X10;
  // brightnessBias is a 1-byte append that fits in the old tail padding (sizeof
  // unchanged) — same trap as gammaX10; old blobs read back the padding byte
  if (cfg.brightnessBias < -50 || cfg.brightnessBias > 50) cfg.brightnessBias = 0;

  // Rewrite in the current size so the next boot is a clean full load.
  prefs.putUShort("ver", SCHEMA_VERSION);
  prefs.putBytes("blob", &cfg, sizeof(Settings));
  prefs.end();
}

static void saveLocked() {
  prefs.begin(NS, false);
  prefs.putUShort("ver", SCHEMA_VERSION);
  prefs.putBytes("blob", &settings::cfg, sizeof(Settings));
  prefs.end();
}

void settings::save() {
  if (gMx) xSemaphoreTake(gMx, portMAX_DELAY);
  saveLocked();
  if (gMx) xSemaphoreGive(gMx);
}

bool settings::ascEnabled() {
  return cfg.profile == PROFILE_VENTILATED;
}

void settings::markRecalibrated(uint32_t epoch) {
  if (gMx) xSemaphoreTake(gMx, portMAX_DELAY);
  cfg.lastFrcEpoch = epoch;          // field write inside the lock, so a web
  saveLocked();                      // save can't persist a blob without it
  if (gMx) xSemaphoreGive(gMx);
}
